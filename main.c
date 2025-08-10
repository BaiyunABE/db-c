#include "bptree.h"
#include <stdio.h>
#include <string.h>
int main() {
  init("test");
  const char* s = "data 0";
  insert(0, s, strlen(s) + 1);
  data_t* data = find(0);
  fwrite(data->data, sizeof(*data->data), data->size, stdout);
  fflush(stdout);
  erase(0);
  data = find(0);
  fwrite(data->data, sizeof(*data->data), data->size, stdout);
  fflush(stdout);
  return 0;
}
