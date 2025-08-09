/*
 * bptree.h
 */
#ifndef _BPTREE_H_
#define _BPTREE_H_

#include <stdint.h>

void init(const char*& fn);
int insert(uint64_t key, char* data);
char* find(uint64_t key);
char** find_range(uint64_t left, uint64_t right);
int erase(uint64_t key);
int update(uint64_t key, char* data);
void destroy();

#endif // _BPTREE_H_
