/*
 * bptree.c
 *
 * - file pointer's position is unknown after you call any function.
*/
#include "bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ORDER 254
#define NODE_SIZE (sizeof(bpnode) + sizeof(header_t))
#define MAGIC 0x1234567
#define BRUNCH 0x01
#define LEAF 0x02
#define HEAD 0x0
#define INFINITY 0xffffffffffffffff
#define MIN_BLOCK_SIZE 32

static char* idx_fn;
static char* dat_fn;
static struct {
  uint64_t head;
	uint64_t root;
	uint64_t height;
	uint64_t size;
} idx_header;
static struct {
  uint64_t head;
  uint64_t size;
} dat_header;
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

/*
 * do initialization of bptree.
 * - if file exist, open the file and read the header.
 * - if not, create the file, initialize the header and the free list.
 */
void init(const char* fn) {
  int len = strlen(fn);

  idx_fn = malloc(len + 5);
  strcpy(idx_fn, fn);
  strcpy(idx_fn + len, ".idx");
	idx_fp = fopen(idx_fn, "rb+");

  if (idx_fp == NULL) {
    idx_fp = fopen(idx_fn, "wb+");

    // write header
    idx_header.head = sizeof(idx_header);
    idx_header.root = 0;
    idx_header.height = 0;
    idx_header.size = 0;
    fwrite(&idx_header, sizeof(idx_header), 1, idx_fp);

    // write head node
    header_t header;
    header.size = INFINITY;
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

    // write header
    dat_header.head = sizeof(dat_header);
    dat_header.size = 0;
    fwrite(&dat_header, sizeof(dat_header), 1, dat_fp);

    // write head node
    header_t header;
    header.size = INFINITY;
    header.next = 0;
    fwrite(&header, sizeof(header), 1, dat_fp);
  }
  else {
    fread(&dat_header, sizeof(dat_header), 1, dat_fp);
  }
}

/*
 * update idx_header to file
 */
static void update_idx_header() {
	fseek(idx_fp, HEAD, SEEK_SET);
  fwrite(&idx_header, sizeof(idx_header), 1, idx_fp);
	fflush(idx_fp);
}

/*
 * read one node
 */
static void read_node(bpnode* node, uint64_t offset) {
	fseek(idx_fp, offset, SEEK_SET);
  fread(node, sizeof(*node), 1, idx_fp);
}

/*
 * write one node
 */
static void update_node(const bpnode* node, uint64_t offset) {
	fseek(idx_fp, offset, SEEK_SET);
	fwrite(node, sizeof(*node), 1, idx_fp);
  fflush(idx_fp);
}

/*
 * allocate a space for a node and write it
 * return the offset of the new node
 */
static uint64_t alloc_node(const bpnode* node) {
	uint64_t offset = idx_header.head + sizeof(header_t); // return ptr to allocated space

  header_t header;
  fseek(idx_fp, idx_header.head, SEEK_SET);
  fread(&header, sizeof(header), 1, idx_fp);

	if (header.size == sizeof(bpnode)) { // allocate the hole block
		uint64_t magic = MAGIC;
    fseek(idx_fp, idx_header.head + sizeof(header.size), SEEK_SET);
    fwrite(&magic, sizeof(magic), 1, idx_fp);

    idx_header.head = header.next;
	}
	else { // split
    fseek(idx_fp, idx_header.head + NODE_SIZE, SEEK_SET);
    fwrite(&header, sizeof(header), 1, idx_fp);

    header.size = sizeof(bpnode);
    header.next = MAGIC;
    fseek(idx_fp, idx_header.head, SEEK_SET);
    fwrite(&header, sizeof(header), 1, idx_fp);

    idx_header.head += NODE_SIZE;
	}

	update_node(node, offset);

	idx_header.size++;
  update_idx_header();

	return offset;
}

/*
 * free a node allocated by `alloc_node()`
 * if offset is illegal, abort
 */
static void free_node(uint64_t offset) {
  header_t header;

  offset -= sizeof(header_t);
  fseek(idx_fp, offset, SEEK_SET);
  fread(&header, sizeof(header), 1, idx_fp);

  assert(header.next == MAGIC);

  header.next = idx_header.head;
  fseek(idx_fp, offset, SEEK_SET);
  fwrite(&header, sizeof(header), 1, idx_fp);

  idx_header.head = offset;
  idx_header.size--;
  update_idx_header();
}

/*
 * read one data
 *
 * when data is used,
 * - free(data->data);
 * - free(data);
 */
static data_t* read_data(uint64_t offset) {
  fseek(dat_fp, offset, SEEK_SET);

  data_t* data = malloc(sizeof(data_t));
	fread(&data->size, sizeof(data->size), 1, dat_fp);

	data->data = malloc(data->size * sizeof(char));
	fread(data->data, sizeof(*data->data), data->size, dat_fp);

	return data;
}

/*
 * allocate a space for a data and write it
 * return the offset of the new data
 */
uint64_t alloc_data(const char* data, uint64_t size) {
  uint64_t size_tmp = size + sizeof(uint64_t);
  size_tmp = (((size_tmp >> 4) + ((size_tmp & 0xf) != 0)) << 4); // ((size_tmp + 15) // 16) * 16

  header_t header;

	uint64_t pp = HEAD;
  uint64_t p;
  uint64_t best;
  uint64_t pbest = 0;
  uint64_t ppbest;

  fseek(dat_fp, pp, SEEK_SET);
  fread(&p, sizeof(p), 1, dat_fp);

  while (p != 0) {
    fseek(dat_fp, p, SEEK_SET);
    fread(&header, sizeof(header), 1, dat_fp);

    if (header.size >= size_tmp && (pbest == 0 || (header.size < best))) {
      best = header.size;
      pbest = p;
      ppbest = pp;
    }

    pp = p + sizeof(header.size);
    p = header.next;
  }

	if (pbest != 0) {
		uint64_t offset = pbest + sizeof(header_t); // return ptr to allocated space

    fseek(dat_fp, pbest, SEEK_SET);
    fread(&header, sizeof(header), 1, dat_fp);

		if (header.size - size_tmp < MIN_BLOCK_SIZE) { // allocate the hole block
		  uint64_t magic = MAGIC;
      fseek(dat_fp, pbest + sizeof(header.size), SEEK_SET);
      fwrite(&magic, sizeof(magic), 1, dat_fp);

      fseek(dat_fp, ppbest, SEEK_SET);
      fwrite(&header.next, sizeof(header.next), 1, dat_fp);
		}
		else { // split
      header.size -= (sizeof(header_t) + size_tmp);
      fseek(dat_fp, pbest + sizeof(header_t) + size_tmp, SEEK_SET);
      fwrite(&header, sizeof(header), 1, dat_fp);

      header.size = size_tmp;
      header.next = MAGIC;
      fseek(dat_fp, pbest, SEEK_SET);
      fwrite(&header, sizeof(header), 1, dat_fp);
      
      pbest += (sizeof(header_t) + size_tmp);
      fseek(dat_fp, ppbest, SEEK_SET);
      fwrite(&pbest, sizeof(pbest), 1, dat_fp);
		}

    fseek(dat_fp, HEAD, SEEK_SET);
    fread(&dat_header, sizeof(dat_header), 1, dat_fp);
		dat_header.size++;
    fseek(dat_fp, HEAD, SEEK_SET);
    fwrite(&dat_header, sizeof(dat_header), 1, dat_fp);

    fseek(dat_fp, offset, SEEK_SET);
    fwrite(&size, sizeof(size), 1, dat_fp);
    fwrite(data, sizeof(*data), size, dat_fp);
    fflush(dat_fp);
		return offset;
	}
	else {
		return 0;
	}
}

void free_data(uint64_t offset) {
	header_t header;

  offset -= sizeof(header_t);

  fseek(dat_fp, offset, SEEK_SET);
  fread(&header, sizeof(header), 1, dat_fp);

  assert(header.next == MAGIC);
	
	uint64_t pp = HEAD;
  uint64_t p;

  fseek(dat_fp, pp, SEEK_SET);
  fread(&p, sizeof(p), 1, dat_fp);

  while (p != 0 && p < offset) {
    pp = p + sizeof(header.size);
    fseek(dat_fp, pp, SEEK_SET);
    fread(&p, sizeof(p), 1, dat_fp);
  }

	header.next = p;
	p = offset;
	
	// merge
	
	uint64_t next_off = header.next;

	if (next_off != 0 && offset + sizeof(header_t) + header.size == next_off) {
    header_t next; 
    fseek(dat_fp, next_off, SEEK_SET);
    fread(&next, sizeof(next), 1, dat_fp);
		
    header.size += sizeof(header_t) + next.size;
		header.next = next.next;
    fseek(dat_fp, offset, SEEK_SET);
    fwrite(&header, sizeof(header), 1, dat_fp);
	}
  else {
    fseek(dat_fp, offset, SEEK_SET);
    fwrite(&header, sizeof(header), 1, dat_fp);
  }
	
	if (pp != HEAD) {
    uint64_t prev_off = pp - sizeof(header.size);
    header_t prev;
    fseek(dat_fp, prev_off, SEEK_SET);
    fread(&prev, sizeof(prev), 1, dat_fp);

	  if (prev_off + sizeof(header_t) + prev.size == offset) {
	  	prev.size += sizeof(header_t) + header.size;
	  	prev.next = header.next;
      fseek(dat_fp, prev_off, SEEK_SET);
      fwrite(&prev, sizeof(prev), 1, dat_fp);
	  }
    else {
      fseek(dat_fp, pp, SEEK_SET);
      fwrite(&p, sizeof(p), 1, dat_fp);
    }
  }
  else {
    fseek(dat_fp, pp, SEEK_SET);
    fwrite(&p, sizeof(p), 1, dat_fp);
  }

    fseek(dat_fp, HEAD, SEEK_SET);
    fread(&dat_header, sizeof(dat_header), 1, dat_fp);
		dat_header.size--;
    fseek(dat_fp, HEAD, SEEK_SET);
    fwrite(&dat_header, sizeof(dat_header), 1, dat_fp);
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
	if (left.type == LEAF)
		right.next = left.next;
	// set left
	left.size = ORDER / 2;
	// set p
	for (int j = parent.size - 1; j > i; j--)
		parent.children[j + 1] = parent.children[j];
	parent.children[i + 1] = alloc_node(&right);
	if (left.type == LEAF)
		left.next = parent.children[i + 1];
	for (int j = parent.size - 1; j >= i; j--)
		parent.keys[j + 1] = parent.keys[j];
	parent.keys[i] = left.keys[ORDER / 2 - 1];
	parent.size++;
	update_node(&parent, offset);
	update_node(&left, parent.children[i]);
	update_node(&right, parent.children[i + 1]);
}

static int insert_nonfull(uint64_t offset, uint64_t key, const char* data, uint64_t size) {
	// read node
	bpnode root;
	read_node(&root, offset);
	// insert
	if (root.type == LEAF) {
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
		update_node(&root, offset);
		return 1;
	}
	else {
		int i;
		for (i = 0; i < root.size && key > root.keys[i]; i++);
		if (i == root.size) {
			i--;
			root.keys[i] = key;
			update_node(&root, offset);
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
	if (idx_header.root == 0) {
		bpnode root;
    root.type = 0x02; // leaf
    root.size = 1;
    root.keys[0] = key;
		root.next = 0x0; // null
		root.children[0] = alloc_data(data, size);
		idx_header.root = alloc_node(&root);
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
			idx_header.root = alloc_node(&parent);
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
	if (root.type == BRUNCH) {
		int i;
		for (i = 0; i < root.size && key > root.keys[i]; i++);
		if (i == root.size)
			return 0xffffffffffffffff;
		else
			return find_recursive(key, root.children[i]);
	}
	else {
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
	update_node(&left, root.children[i]);
	// set right
	free_node(root.children[i + 1]);
	// set root
	root.size--;
	for (int j = i; j < root.size; j++)
		root.keys[j] = root.keys[j + 1];
	for (int j = i + 1; j < root.size; j++)
		root.children[j] = root.children[j + 1];
	update_node(&root, offset);
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
	else if (root.type == LEAF) {
		if (root.keys[i] != key)
			return 0;
		else {
      free_data(root.children[i]);
			root.size--;
			for (int j = i; j < root.size; j++)
				root.keys[j] = root.keys[j + 1];
			for (int j = i; j < root.size; j++)
				root.children[j] = root.children[j + 1];
			update_node(&root, offset);
			return 1;
		}
	}
	else {
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
					update_node(&node, root.children[i]);
					// set left
					left.size--;
					update_node(&left, root.children[i - 1]);
					// set root
					root.keys[i - 1] = left.keys[left.size - 1];
					update_node(&root, offset);
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
					update_node(&node, root.children[i]);
					// set right
					right.size--;
					for (int j = 0; j < right.size; j++)
						right.keys[j] = right.keys[j + 1];
					for (int j = 0; j < right.size; j++)
						right.children[j] = right.children[j + 1];
					update_node(&right, root.children[i + 1]);
					// set root
					root.keys[i] = node.keys[node.size - 1];
					update_node(&root, offset);
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
			update_node(&root, offset);
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
	while (root.size == 1 && root.type == BRUNCH) {
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
		erase(key);
		insert(key, data, size);
	}
	return 1;
}

void destroy() {
	update_idx_header();
	if (idx_fp != NULL)
		fclose(idx_fp);
}
