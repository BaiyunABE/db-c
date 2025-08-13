#include "bptree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ans[1000];

int func(char* s) { 
  s[5] = (rand() % 10) ^ '0';
  s[6] = (rand() % 10) ^ '0';
  s[7] = (rand() % 10) ^ '0';
  int num = s[5] ^ '0';
  num *= 10;
  num += (s[6] ^ '0');
  num *= 10;
  num += (s[7] ^ '0');
  return num;
}

int main() {
  for (int i = 0; i < 1000; i++) {
    ans[i] = 0;
  }
  init("test");
  char* s = strdup("data 000");
  int len = strlen(s) + 1;
  while (1) {
    int num = func(s);
    int r = rand();
    if (r % 3 == 0) {
      int rv = insert(num, s, len);
      printf("insert: %d, %d\n", num, rv);
      if ((rv ^ ans[num]) == 0) {
        printf("insert fail\n");
        abort();
      }
      ans[num] = 1;
    }
    else if (r % 3 == 1) {
      data_t* data = find(num);
      printf("find: %d, %s\n", num, data->size > 0 ? data->data : "null");
      if ((data->size ^ ans[num]) == 1) {
        printf("find fail\n");
        abort();
      }
    }
    else {
      int rv = erase(num);
      printf("erase: %d, %d\n", num, rv);
      if ((rv ^ ans[num]) == 1) {
        printf("erase fail\n");
        abort();
      }
      ans[num] = 0;
    }
  }
  return 0;
}
