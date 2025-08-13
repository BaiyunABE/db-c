/*
 * bptree.h
 */
#ifndef _BPTREE_H_
#define _BPTREE_H_

#include <stdint.h>

typedef struct {
  uint64_t size;
  char* data;
} data_t;

void init(const char* fn);

int insert(uint64_t key, const char* data, uint64_t size);

data_t* find(uint64_t key);

// char** find_range(uint64_t left, uint64_t right);

int erase(uint64_t key);

int update(uint64_t key, const char* data, uint64_t size);

void destroy();

#endif // _BPTREE_H_
