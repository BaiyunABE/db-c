/*
 * bptree.c
*/
#include "bptree.h"
#include <stdio.h>
#include <string.h>


#define ORDER 254
#define NODE_SIZE 0x1000

static char* idx_fn;
static char* dat_fn;
static struct {
	uint64_t root_offset;
	uint64_t tree_height;
	uint64_t node_cnt;
} idx_header;
static FILE* idx_fp;
static FILE* dat_fp;

typedef struct {
	uint8_t node_type;
	uint8_t key_cnt;
	uint8_t reserved[6];
	uint64_t keys[ORDER];
	uint64_t children[ORDER];
	uint64_t next_leaf;
} bpnode;

static void init_idx_header() {
	idx_header.root_offset = sizeof idx_header;
	idx_header.tree_height = 0;
	idx_header.node_cnt = 0;
}

void init(const char* fn) {
	idx_fn = fn + ".idx";
	idx_fp.open(idx_fn, in | out | binary);
	if (!idx_fp.is_open()) {
		idx_fp.clear();
		FILE* tmp(idx_fn, out | binary);
		tmp.close();
		idx_fp.open(idx_fn, in | out | binary);
		init_idx_header();
	}
	else
		idx_fp.read(reinterpret_cast<char*>(&idx_header), sizeof idx_header);

	dat_fn = fn + ".dat";
	dat_fp.open(dat_fn, in | out | binary);
	if (!dat_fp.is_open()) {
		dat_fp.clear();
		FILE* tmp(dat_fn, out | binary);
		tmp.close();
		dat_fp.open(dat_fn, in | out | binary);
	}
}

static void update_idx_header() {
	idx_fp.seekp(0x0);
	idx_fp.write(reinterpret_cast<char*>(&idx_header), sizeof idx_header);
	idx_fp.flush();
}

static void read_idx_node(bpnode* node, uint64_t offset) {
	idx_fp.seekg(offset);
	idx_fp.read(reinterpret_cast<char*>(node), sizeof * node);
}

static char* read_data(uint64_t offset) {
	dat_fp.seekg(offset);
	uint64_t size;
	dat_fp.read(reinterpret_cast<char*>(&size), sizeof size);
	char* data = new char[size + 1];
	dat_fp.read(reinterpret_cast<char*>(data), size + 1);
	char* res = data;
	delete[] data;
	return res;
}

static void update_idx_node(bpnode node, uint64_t offset) {
	idx_fp.seekp(offset);
	idx_fp.write(reinterpret_cast<char*>(&node), sizeof node);
	idx_fp.flush();
}

static uint64_t alloc_idx_node(bpnode node) {
	uint64_t offset = sizeof idx_header + NODE_SIZE * idx_header.node_cnt;
	update_idx_node(node, offset);
	idx_header.node_cnt++;
	return offset;
}

uint64_t alloc_data(const char* data, uint64_t size) {
	dat_fp.seekp(0, end);
	uint64_t offset = dat_fp.tellp();
	dat_fp.write(reinterpret_cast<char*>(&size), sizeof size);
	dat_fp.write(data, size);
	dat_fp.write("\0", 1);
	dat_fp.flush();
	return offset;
}

static int update_data(uint64_t offset, const char* data, uint64_t size) {
	dat_fp.seekg(offset);
	uint64_t cap;
	dat_fp.read(reinterpret_cast<char*>(&cap), sizeof cap);
	if (cap < size)
		return 0;
	dat_fp.seekp(offset);
	dat_fp.write(reinterpret_cast<char*>(&size), sizeof size);
	dat_fp.write(data, size);
	dat_fp.write("\0", 1);
	dat_fp.flush();
	return 1;
}

static void split_ith_child(uint64_t offset, int i) {
	bpnode parent, left;
	read_idx_node(&parent, offset);
	read_idx_node(&left, parent.children[i]);
	// set right
	bpnode right(left.node_type, ORDER / 2);
	for (int j = 0; j < ORDER / 2; j++)
		right.keys[j] = left.keys[j + ORDER / 2];
	for (int j = 0; j < ORDER / 2; j++)
		right.children[j] = left.children[j + ORDER / 2];
	if (left.node_type == 0x02) // leaf
		right.next_leaf = left.next_leaf;
	// set left
	left.key_cnt = ORDER / 2;
	// set p
	for (int j = parent.key_cnt - 1; j > i; j--)
		parent.children[j + 1] = parent.children[j];
	parent.children[i + 1] = alloc_idx_node(right);
	if (left.node_type == 0x02) // leaf
		left.next_leaf = parent.children[i + 1];
	for (int j = parent.key_cnt - 1; j >= i; j--)
		parent.keys[j + 1] = parent.keys[j];
	parent.keys[i] = left.keys[ORDER / 2 - 1];
	parent.key_cnt++;
	update_idx_node(parent, offset);
	update_idx_node(left, parent.children[i]);
	update_idx_node(right, parent.children[i + 1]);
}

static int insert_nonfull(uint64_t offset, uint64_t key, char* data) {
	// read node
	bpnode root;
	read_idx_node(&root, offset);
	// insert
	if (root.node_type == 0x02) { // root is leaf
		for (int i = 0; i < root.key_cnt; i++)
			if (root.keys[i] == key)
				return 0;
		int i;
		for (i = root.key_cnt - 1; i >= 0 && key < root.keys[i]; i--)
			root.keys[i + 1] = root.keys[i];
		for (i = root.key_cnt - 1; i >= 0 && key < root.keys[i]; i--)
			root.children[i + 1] = root.children[i];
		root.keys[i + 1] = key;
		root.children[i + 1] = alloc_data(data.c_str(), data.size());
		root.key_cnt++;
		update_idx_node(root, offset);
		return 1;
	}
	else { // root is brunch
		int i;
		for (i = 0; i < root.key_cnt && key > root.keys[i]; i++);
		if (i == root.key_cnt) {
			i--;
			root.keys[i] = key;
			update_idx_node(root, offset);
		}
		bpnode node;
		read_idx_node(&node, root.children[i]);
		if (node.key_cnt == ORDER) {
			split_ith_child(offset, i);
			read_idx_node(&root, offset);
			if (key > root.keys[i])
				i++;
		}
		return insert_nonfull(root.children[i], key, data);
	}
}

int insert(uint64_t key, char* data) {
	if (idx_header.tree_height == 0) {
		bpnode root(0x02, 1); // leaf
		root.keys[0] = key;
		root.next_leaf = 0x0; // null
		root.children[0] = alloc_data(data.c_str(), data.size());
		alloc_idx_node(root);
		idx_header.tree_height++;
		return 1;
	}
	else {
		bpnode root;
		read_idx_node(&root, idx_header.root_offset);
		if (root.key_cnt == ORDER) { // root is full
			bpnode parent;
			parent.node_type = 0x01; // brunch
			parent.key_cnt = 1;
			parent.keys[0] = root.keys[ORDER - 1];
			parent.children[0] = idx_header.root_offset;
			idx_header.root_offset = alloc_idx_node(parent);
			split_ith_child(idx_header.root_offset, 0);
			idx_header.tree_height++;
		}
		return insert_nonfull(idx_header.root_offset, key, data);
	}
}

static uint64_t find_recursive(uint64_t key, uint64_t offset) {
	bpnode root;
	read_idx_node(&root, offset);
	if (root.node_type == 0x01) { // brunch
		int i;
		for (i = 0; i < root.key_cnt && key > root.keys[i]; i++);
		if (i == root.key_cnt)
			return 0xffffffffffffffff;
		else
			return find_recursive(key, root.children[i]);
	}
	else { // leaf
		int i;
		for (i = 0; i < root.key_cnt && key != root.keys[i]; i++);
		if (i < root.key_cnt)
			return root.children[i];
		else
			return 0xffffffffffffffff;
	}
}

char* find(uint64_t key) {
	if (idx_header.tree_height == 0)
		return "null";
	else {
		uint64_t offset = find_recursive(key, idx_header.root_offset);
		if (offset == 0xffffffffffffffff)
			return "null";
		else
			return read_data(offset);
	}
}

static uint64_t find_idx_node_recursive(uint64_t key, uint64_t offset) {
	bpnode root;
	read_idx_node(&root, offset);
	if (key > root.keys[root.key_cnt - 1])
		return 0xffffffffffffffff;
	if (root.node_type == 0x01) { // brunch
		int i;
		for (i = 0; i < root.key_cnt && key > root.keys[i]; i++);
		return find_idx_node_recursive(key, root.children[i]);
	}
	else // leaf
		return offset;
}

char** find_range(uint64_t left, uint64_t right) {
	vector<char*> res;
	if (idx_header.tree_height == 0)
		return res;
	else {
		uint64_t offset = find_idx_node_recursive(left, idx_header.root_offset);
		if (offset == 0xffffffffffffffff)
			return res;
		else {
			bpnode leaf;
			do {
				read_idx_node(&leaf, offset);
				for (int i = 0; i < leaf.key_cnt; i++)
					if (leaf.keys[i] >= left && leaf.keys[i] < right)
						res.push_back(read_data(leaf.children[i]));
				offset = leaf.next_leaf;
			} while (leaf.keys[leaf.key_cnt - 1] < right && offset != 0x0);
			return res;
		}
	}
}

static void free_idx_node(uint64_t offset) {
	// ...
}

static void merge_child(uint64_t offset, int i) {
	bpnode root, left, right;
	read_idx_node(&root, offset);
	read_idx_node(&left, root.children[i]);
	read_idx_node(&right, root.children[i + 1]);
	// set left
	for (int j = 0; j < ORDER / 2; j++)
		left.keys[j + ORDER / 2] = right.keys[j];
	for (int j = 0; j < ORDER / 2; j++)
		left.children[j + ORDER / 2] = right.children[j];
	left.key_cnt = ORDER;
	update_idx_node(left, root.children[i]);
	// set right
	free_idx_node(root.children[i + 1]);
	// set root
	root.key_cnt--;
	for (int j = i; j < root.key_cnt; j++)
		root.keys[j] = root.keys[j + 1];
	for (int j = i + 1; j < root.key_cnt; j++)
		root.children[j] = root.children[j + 1];
	update_idx_node(root, offset);
}

static int find_idx(bpnode& node, uint64_t key) {
	int i;
	for (i = 0; i < node.key_cnt; i++)
		if (node.keys[i] >= key)
			break;
	return i;
}

static int erase_nonunderflow(uint64_t offset, uint64_t key) {
	bpnode root;
	read_idx_node(&root, offset);
	int i = find_idx(root, key);
	if (i >= root.key_cnt)
		return 0;
	else if (root.node_type == 0x02) { // leaf
		if (root.keys[i] != key)
			return 0;
		else {
			root.key_cnt--;
			for (int j = i; j < root.key_cnt; j++)
				root.keys[j] = root.keys[j + 1];
			for (int j = i; j < root.key_cnt; j++)
				root.children[j] = root.children[j + 1];
			update_idx_node(root, offset);
			return 1;
		}
	}
	else { // brunch
		bpnode node;
		read_idx_node(&node, root.children[i]);
		if (node.key_cnt == ORDER / 2) { // underflow
			int underflow = 1;
			if (i > 0) { // left exist
				bpnode left;
				read_idx_node(&left, root.children[i - 1]);
				if (left.key_cnt != ORDER / 2) { // left is not underflow
					// set node
					for (int j = ORDER / 2; j > 0; j--)
						node.keys[j] = node.keys[j - 1];
					node.keys[0] = left.keys[left.key_cnt - 1];
					for (int j = ORDER / 2; j > 0; j--)
						node.children[j] = node.children[j - 1];
					node.children[0] = left.children[left.key_cnt - 1];
					if (node.node_type == 0x01)   // leaf
						node.children[0] = left.children[left.key_cnt - 1];
					node.key_cnt++;
					update_idx_node(node, root.children[i]);
					// set left
					left.key_cnt--;
					update_idx_node(left, root.children[i - 1]);
					// set root
					root.keys[i - 1] = left.keys[left.key_cnt - 1];
					update_idx_node(root, offset);
					underflow = 0;
				}
			}
			if (underflow && i < root.key_cnt - 1) {
				bpnode right;
				read_idx_node(&right, root.children[i + 1]);
				if (right.key_cnt != ORDER / 2) { // right is not underflow
					// set node
					node.keys[node.key_cnt] = right.keys[0];
					node.children[node.key_cnt] = right.children[0];
					node.key_cnt++;
					update_idx_node(node, root.children[i]);
					// set right
					right.key_cnt--;
					for (int j = 0; j < right.key_cnt; j++)
						right.keys[j] = right.keys[j + 1];
					for (int j = 0; j < right.key_cnt; j++)
						right.children[j] = right.children[j + 1];
					update_idx_node(right, root.children[i + 1]);
					// set root
					root.keys[i] = node.keys[node.key_cnt - 1];
					update_idx_node(root, offset);
					underflow = 0;
				}
			}
			if (underflow) {
				if (i < root.key_cnt - 1)
					merge_child(offset, i);
				else {
					merge_child(offset, i - 1);
					i--;
				}
			}
		}
		int res = erase_nonunderflow(root.children[i], key);
		read_idx_node(&root, offset);
		read_idx_node(&node, root.children[i]);
		if (root.keys[i] != node.keys[node.key_cnt - 1]) {
			root.keys[i] = node.keys[node.key_cnt - 1];
			update_idx_node(root, offset);
		}
		return res;
	}
}

int erase(uint64_t key) {
	if (idx_header.tree_height == 0)
		return 0;
	int res = erase_nonunderflow(idx_header.root_offset, key);
	bpnode root;
	read_idx_node(&root, idx_header.root_offset);
	if (root.key_cnt == 0)
		init_idx_header();
	while (root.key_cnt == 1 && root.node_type == 0x01) { // brunch
		free_idx_node(idx_header.root_offset);
		idx_header.root_offset = root.children[0];
		idx_header.tree_height--;
		read_idx_node(&root, idx_header.root_offset);
	}
	return res;
}

int update(uint64_t key, char* data) {
	if (idx_header.tree_height == 0)
		return 0;
	uint64_t offset = find_recursive(key, idx_header.root_offset);
	if (offset == 0xffffffffffffffff)
		return 0;
	else {
		if (!update_data(offset, data.c_str(), data.size())) {
			erase(key);
			insert(key, data);
		}
	}
	return 1;
}

void destroy() {
	update_idx_header();
	if (idx_fp.is_open())
		idx_fp.close();
}
