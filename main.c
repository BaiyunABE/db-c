#include "bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main() {
  init("test");
  char* s = strdup("data 000");
  int len = strlen(s) + 1;
  while (1) {
    s[5] = (rand() % 10) ^ '0';
    s[6] = (rand() % 10) ^ '0';
    s[7] = (rand() % 10) ^ '0';
    int num = s[5] ^ '0';
    num *= 10;
    num += (s[6] ^ '0');
    num *= 10;
    num += (s[7] ^ '0');
    int r = rand();
    if (r % 3 == 0) {
      int rv = insert(num, s, len);
      printf("insert: %d, %d\n", num, rv);
    }
    else if (r % 3 == 1) {
      data_t* data = find(num);
      printf("find: %d, %s\n", num, data->size > 0 ? data->data : "null");
    }
    else {
      int rv = erase(num);
      printf("erase: %d, %d\n", num, rv);
    }
  }
  return 0;
}
