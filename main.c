#include "bptree.h"
#include <stdio.h>

int main() {
  init("test");
  insert(0, "data 0");
  puts(find(0));
  erase(0);
  puts(find(0));
  return 0;
}
