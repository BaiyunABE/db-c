/*
 * bptree.c
*/
#include "bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORDER 4
#define NODE_SIZE (sizeof(bpnode) + sizeof(header_t))
#define MAGIC 0x1234567

static char* idx_fn;
static char* dat_fn;
static struct {
  uint64_t head;
	uint64_t root;
	uint64_t height;
	uint64_t size;
} idx_header;
static FILE* idx_fp;
static FILE* dat_fp;

typedef struct {
	size_t size;
	uint64_t next;
} header_t;

typedef struct {
	uint8_t type;
	uint8_t size;
	uint8_t reserved[6];
	uint64_t keys[ORDER];
	uint64_t children[ORDER];
	uint64_t next;
} bpnode;

void init(const char* fn) {
  int len = strlen(fn);
  idx_fn = malloc(len + 5);
  strcpy(idx_fn, fn);
  strcpy(idx_fn + len, ".idx");
	idx_fp = fopen(idx_fn, "rb+");
  if (idx_fp == NULL) {
    idx_fp = fopen(idx_fn, "wb+");
    idx_header.head = sizeof(idx_header);
    idx_header.root = 0;
    idx_header.height = 0;
    idx_header.size = 0;
    fwrite(&idx_header, sizeof(idx_header), 1, idx_fp);
    header_t header;
    header.size = 0xffffffffffffffff;
    header.next = 0;
    fwrite(&header, sizeof(header), 1, idx_fp);
  }
  else {
    fread(&idx_header, sizeof(idx_header), 1, idx_fp);
  }
  dat_fn = malloc(len + 5);
	strcpy(dat_fn, fn);
  strcpy(dat_fn + len, ".dat");
	dat_fp = fopen(dat_fn, "rb+");
  if (dat_fp == NULL) {
    dat_fp = fopen(dat_fn, "wb+");
  }
}

static void update_idx_header() {
	fseek(idx_fp, 0x0, SEEK_SET);
  fwrite(&idx_header, sizeof(idx_header), 1, idx_fp);
	fflush(idx_fp);
}

static void read_node(bpnode* node, uint64_t offset) {
	fseek(idx_fp, offset, SEEK_SET);
  fread(node, sizeof(*node), 1, idx_fp);
}

static data_t* read_data(uint64_t offset) {
	fseek(dat_fp, offset, SEEK_SET);
	data_t* data = malloc(sizeof(data_t));
	fread(&data->size, sizeof(data->size), 1, dat_fp);
	data->data = malloc(data->size * sizeof(char));
	fread(data->data, sizeof(*data->data), data->size, dat_fp);
	return data;
}

static void update_node(bpnode node, uint64_t offset) {
	fseek(idx_fp, offset, SEEK_SET);
	fwrite(&node, sizeof(node), 1, idx_fp);
  fflush(idx_fp);
}

static uint64_t alloc_node(bpnode node) {
	uint64_t offset = idx_header.head + sizeof(header_t); // return ptr to allocated space
  header_t header;
  fseek(idx_fp, idx_header.head, SEEK_SET);
  fread(&header, sizeof(header), 1, idx_fp);
	if (header.size == sizeof(bpnode)) { // allocate the hole block
    idx_header.head = header.next;
		header.next = MAGIC;
    printf("write MAGIC to 0x%lx\n", idx_header.head);
    fseek(idx_fp, idx_header.head, SEEK_SET);
    fwrite(&header, sizeof(header), 1, idx_fp);
	}
	else { // split
    fseek(idx_fp, idx_header.head + NODE_SIZE, SEEK_SET);
    fwrite(&header, sizeof(header), 1, idx_fp);
    header.size = sizeof(bpnode);
    header.next = MAGIC;
    printf("write MAGIC to 0x%lx\n", idx_header.head);
    fseek(idx_fp, idx_header.head, SEEK_SET);
    fwrite(&header, sizeof(header), 1, idx_fp);
    idx_header.head += NODE_SIZE;
	}

	update_node(node, offset);
	idx_header.size++;
  update_idx_header();
	return offset;
}

static void free_node(uint64_t offset) {
  header_t header;
  offset -= sizeof(header_t);
  fseek(idx_fp, offset, SEEK_SET);
  fread(&header, sizeof(header), 1, idx_fp);
  if (header.next != MAGIC) {
    fprintf(stderr, "error: free_node: wrong magic number.\n");
  }

  header.next = idx_header.head;
  fseek(idx_fp, offset, SEEK_SET);
  fwrite(&header, sizeof(header), 1, idx_fp);

  idx_header.head = offset;
  idx_header.size--;
  update_idx_header();
}

uint64_t alloc_data(const char* data, uint64_t size) {
  fseek(dat_fp, 0, SEEK_END);
	uint64_t offset = ftell(dat_fp);
  fwrite(&size, sizeof(size), 1, dat_fp);
  fwrite(data, sizeof(*data), size, dat_fp);
  fflush(dat_fp);
	return offset;
}

static int update_data(uint64_t offset, const char* data, uint64_t size) {
	fseek(dat_fp, offset, SEEK_SET);
	uint64_t cap;
  fread(&cap, sizeof(cap), 1, dat_fp);
	if (cap < size)
		return 0;
  fseek(dat_fp, offset, SEEK_SET);
  fwrite(&size, sizeof(size), 1, dat_fp);
  fwrite(data, sizeof(*data), size, dat_fp);
	fflush(dat_fp);
  return 1;
}

static void split_ith_child(uint64_t offset, int i) {
	bpnode parent, left, right;
	read_node(&parent, offset);
	read_node(&left, parent.children[i]);
	// set right
  right.type = left.type;
  right.size = ORDER / 2;
	for (int j = 0; j < ORDER / 2; j++)
		right.keys[j] = left.keys[j + ORDER / 2];
	for (int j = 0; j < ORDER / 2; j++)
		right.children[j] = left.children[j + ORDER / 2];
	if (left.type == 0x02) // leaf
		right.next = left.next;
	// set left
	left.size = ORDER / 2;
	// set p
	for (int j = parent.size - 1; j > i; j--)
		parent.children[j + 1] = parent.children[j];
	parent.children[i + 1] = alloc_node(right);
	if (left.type == 0x02) // leaf
		left.next = parent.children[i + 1];
	for (int j = parent.size - 1; j >= i; j--)
		parent.keys[j + 1] = parent.keys[j];
	parent.keys[i] = left.keys[ORDER / 2 - 1];
	parent.size++;
	update_node(parent, offset);
	update_node(left, parent.children[i]);
	update_node(right, parent.children[i + 1]);
}

static int insert_nonfull(uint64_t offset, uint64_t key, const char* data, uint64_t size) {
	// read node
	bpnode root;
	read_node(&root, offset);
	// insert
	if (root.type == 0x02) { // root is leaf
		for (int i = 0; i < root.size; i++)
			if (root.keys[i] == key)
				return 0;
		int i;
		for (i = root.size - 1; i >= 0 && key < root.keys[i]; i--)
			root.keys[i + 1] = root.keys[i];
		for (i = root.size - 1; i >= 0 && key < root.keys[i]; i--)
			root.children[i + 1] = root.children[i];
		root.keys[i + 1] = key;
		root.children[i + 1] = alloc_data(data, size);
		root.size++;
		update_node(root, offset);
		return 1;
	}
	else { // root is brunch
		int i;
		for (i = 0; i < root.size && key > root.keys[i]; i++);
		if (i == root.size) {
			i--;
			root.keys[i] = key;
			update_node(root, offset);
		}
		bpnode node;
		read_node(&node, root.children[i]);
		if (node.size == ORDER) {
			split_ith_child(offset, i);
			read_node(&root, offset);
			if (key > root.keys[i])
				i++;
		}
		return insert_nonfull(root.children[i], key, data, size);
	}
}

int insert(uint64_t key, const char* data, uint64_t size) {
	if (idx_header.height == 0) {
		bpnode root;
    root.type = 0x02; // leaf
    root.size = 1;
    root.keys[0] = key;
		root.next = 0x0; // null
		root.children[0] = alloc_data(data, size);
		idx_header.root = alloc_node(root);
		idx_header.height++;
    update_idx_header();
		return 1;
	}
	else {
		bpnode root;
		read_node(&root, idx_header.root);
		if (root.size == ORDER) { // root is full
			bpnode parent;
			parent.type = 0x01; // brunch
			parent.size = 1;
			parent.keys[0] = root.keys[ORDER - 1];
			parent.children[0] = idx_header.root;
			idx_header.root = alloc_node(parent);
			split_ith_child(idx_header.root, 0);
			idx_header.height++;
      update_idx_header();
		}
		return insert_nonfull(idx_header.root, key, data, size);
	}
}

static uint64_t find_recursive(uint64_t key, uint64_t offset) {
	bpnode root;
	read_node(&root, offset);
	if (root.type == 0x01) { // brunch
		int i;
		for (i = 0; i < root.size && key > root.keys[i]; i++);
		if (i == root.size)
			return 0xffffffffffffffff;
		else
			return find_recursive(key, root.children[i]);
	}
	else { // leaf
		int i;
		for (i = 0; i < root.size && key != root.keys[i]; i++);
		if (i < root.size)
			return root.children[i];
		else
			return 0xffffffffffffffff;
	}
}

data_t* find(uint64_t key) {
  if (idx_header.height == 0) {
	  data_t* data = malloc(sizeof(data_t));
    data->size = 0;
    data->data = NULL;
    return data;
  }
	else {
		uint64_t offset = find_recursive(key, idx_header.root);
		if (offset == 0xffffffffffffffff) {
		  data_t* data = malloc(sizeof(data_t));
      data->size = 0;
      data->data = NULL;
      return data;
    }
		else {
			return read_data(offset);
    }
	}
}

/*
static uint64_t find_node_recursive(uint64_t key, uint64_t offset) {
	bpnode root;
	read_node(&root, offset);
	if (key > root.keys[root.size - 1])
		return 0xffffffffffffffff;
	if (root.type == 0x01) { // brunch
		int i;
		for (i = 0; i < root.size && key > root.keys[i]; i++);
		return find_node_recursive(key, root.children[i]);
	}
	else // leaf
		return offset;
}

char** find_range(uint64_t left, uint64_t right) {
	char** res;
	if (idx_header.height == 0)
		return res;
	else {
		uint64_t offset = find_node_recursive(left, idx_header.root);
		if (offset == 0xffffffffffffffff)
			return res;
		else {
			bpnode leaf;
			do {
				read_node(&leaf, offset);
				for (int i = 0; i < leaf.size; i++)
					if (leaf.keys[i] >= left && leaf.keys[i] < right)
						res.push_back(read_data(leaf.children[i])); // use dynamic array
				offset = leaf.next;
			} while (leaf.keys[leaf.size - 1] < right && offset != 0x0);
			return res;
		}
	}
}
*/

static void merge_child(uint64_t offset, int i) {
	bpnode root, left, right;
	read_node(&root, offset);
	read_node(&left, root.children[i]);
	read_node(&right, root.children[i + 1]);
	// set left
	for (int j = 0; j < ORDER / 2; j++)
		left.keys[j + ORDER / 2] = right.keys[j];
	for (int j = 0; j < ORDER / 2; j++)
		left.children[j + ORDER / 2] = right.children[j];
	left.size = ORDER;
	update_node(left, root.children[i]);
	// set right
	free_node(root.children[i + 1]);
	// set root
	root.size--;
	for (int j = i; j < root.size; j++)
		root.keys[j] = root.keys[j + 1];
	for (int j = i + 1; j < root.size; j++)
		root.children[j] = root.children[j + 1];
	update_node(root, offset);
}

static int find_idx(bpnode node, uint64_t key) {
	int i;
	for (i = 0; i < node.size; i++)
		if (node.keys[i] >= key)
			break;
	return i;
}

static int erase_nonunderflow(uint64_t offset, uint64_t key) {
	bpnode root;
	read_node(&root, offset);
	int i = find_idx(root, key);
	if (i >= root.size)
		return 0;
	else if (root.type == 0x02) { // leaf
		if (root.keys[i] != key)
			return 0;
		else {
			root.size--;
			for (int j = i; j < root.size; j++)
				root.keys[j] = root.keys[j + 1];
			for (int j = i; j < root.size; j++)
				root.children[j] = root.children[j + 1];
			update_node(root, offset);
			return 1;
		}
	}
	else { // brunch
		bpnode node;
		read_node(&node, root.children[i]);
		if (node.size == ORDER / 2) { // underflow
			int underflow = 1;
			if (i > 0) { // left exist
				bpnode left;
				read_node(&left, root.children[i - 1]);
				if (left.size != ORDER / 2) { // left is not underflow
					// set node
					for (int j = ORDER / 2; j > 0; j--)
						node.keys[j] = node.keys[j - 1];
					node.keys[0] = left.keys[left.size - 1];
					for (int j = ORDER / 2; j > 0; j--)
						node.children[j] = node.children[j - 1];
					node.children[0] = left.children[left.size - 1];
					if (node.type == 0x01)   // leaf
						node.children[0] = left.children[left.size - 1];
					node.size++;
					update_node(node, root.children[i]);
					// set left
					left.size--;
					update_node(left, root.children[i - 1]);
					// set root
					root.keys[i - 1] = left.keys[left.size - 1];
					update_node(root, offset);
					underflow = 0;
				}
			}
			if (underflow && i < root.size - 1) {
				bpnode right;
				read_node(&right, root.children[i + 1]);
				if (right.size != ORDER / 2) { // right is not underflow
					// set node
					node.keys[node.size] = right.keys[0];
					node.children[node.size] = right.children[0];
					node.size++;
					update_node(node, root.children[i]);
					// set right
					right.size--;
					for (int j = 0; j < right.size; j++)
						right.keys[j] = right.keys[j + 1];
					for (int j = 0; j < right.size; j++)
						right.children[j] = right.children[j + 1];
					update_node(right, root.children[i + 1]);
					// set root
					root.keys[i] = node.keys[node.size - 1];
					update_node(root, offset);
					underflow = 0;
				}
			}
			if (underflow) {
				if (i < root.size - 1)
					merge_child(offset, i);
				else {
					merge_child(offset, i - 1);
					i--;
				}
			}
		}
		int res = erase_nonunderflow(root.children[i], key);
		read_node(&root, offset);
		read_node(&node, root.children[i]);
		if (root.keys[i] != node.keys[node.size - 1]) {
			root.keys[i] = node.keys[node.size - 1];
			update_node(root, offset);
		}
		return res;
	}
}

int erase(uint64_t key) {
	if (idx_header.root == 0)
		return 0;
	int res = erase_nonunderflow(idx_header.root, key);
	bpnode root;
	read_node(&root, idx_header.root);
	if (root.size == 0) { // need reset file?
    free_node(idx_header.root);
    idx_header.root = 0;
    idx_header.height = 0;
  }
	while (root.size == 1 && root.type == 0x01) { // brunch
		free_node(idx_header.root);
		idx_header.root = root.children[0];
		idx_header.height--;
		read_node(&root, idx_header.root);
	}
  update_idx_header();
	return res;
}

int update(uint64_t key, const char* data, uint64_t size) {
	if (idx_header.root == 0)
		return 0;
	uint64_t offset = find_recursive(key, idx_header.root);
	if (offset == 0xffffffffffffffff)
		return 0;
	else {
		if (!update_data(offset, data, size)) {
			erase(key);
			insert(key, data, size);
		}
	}
	return 1;
}

void destroy() {
	update_idx_header();
	if (idx_fp != NULL)
		fclose(idx_fp);
}
