# B+ Tree on File

## Index File

- The index file adopts a four hierarchical design.
- User can just operate key and value, need not learn about lower layer.
- Lower layer encapsulates upper layer, and give service to upper layer.

```text
+---------+
|key value|
+---------+
|tree node|
+---------+
|free list|
+---------+
| unix io |
+---------+
```

1. Use C library functions, like `fopen()`, `fclose()`, `fread()`, `fwrite()`, `fseek()` and so on.
2. Use free list and provide function like `alloc_idx_node()`, `free_idx_node()` and so on.
3. Use B+ tree and provide function like `create()`, `insert()`, `erase()`, `update()` and so on.
4. Use finite state machine to support request of application layer, like SQL.

### Index Header
```text
0    8   16   24   32   40   48   56  63
+--------------------------------------+
|                 root                 |
+--------------------------------------+
|                height                |
+--------------------------------------+
|                 size                 |
+--------------------------------------+
```

- `root`: The offset of the root of the B+ tree.
- `height`: The height of the B+ tree.
- `size`: The number of the nodes of the B+ tree.

```c
typedef struct {
  uint64_t root;
  uint64_t height;
  uint64_t size;
} idx_header;
```

### Index Node

#### Free List Node

Free list node encapsulates B+ tree node. Format is below.

```text
0    8   16   24   32   40   48   56  63
+--------------------------------------+
|                 size                 |
+--------------------------------------+
|                 next                 |
+--------------------------------------+
/                                      /
/             B+ tree node             /
/                                      /
+--------------------------------------+
```

- `size`: The size of free space.
- `next`: The next node of free list.
- If node is allocated, `size` should be `0xff0` and `next` should be `MAGIC`.

```c
#define MAGIC 0x1234567

typedef struct {
  uint64_t size;
  uint64_t next;
} free_list_node_header;
```

#### B+ Tree Node

B+ tree node encapsulates data. Format is below.

```
0    8   16   24   32   40   48   56  63
+----+----+----------------------------+
|type|size|          reserved          |
+----+----+----------------------------+
/                                      /
/                 keys                 /
/                                      /
+--------------------------------------+
/                                      /
/               children               /
/                                      /
+--------------------------------------+
|                 next                 |
+--------------------------------------+
```

- `type`: The type of this node. `0x1` for brunch and `0x2` for leaf.
- `size`: The number of keys in this node.
- `reserved`: Unused space for future.
- `keys`: Array of keys of data.
- `children`: Array of children when node is a brunch, or array of addresses of data when node is a leaf.
- `next`: Next B+ tree node.

```c
typedef struct {
  uint8_t type;
  uint8_t size;
  uint8_t reserved[6];
  uint64_t keys[ORDER];
  uint64_t children[ORDER];
  uint64_t next;
} bpnode;
```

#### About Order
*4 * 8B + order * 16B = 4096B* => *order = 254*

```c
#define ORDER 254
```

## Data File

Use hierarchy similar to index file.

```text
+---------+
|   data  |
+---------+
|free list|
+---------+
| unix io |
+---------+
```

Compare with index file,
- Data file has no header now, because no information of hole file need to memory.
- There is no B+ node, but data, maybe string or binary data whose format is defined by user.
- Different allocated node may have different size, which request more intelligent allocating strategy.
- When a B+ node in index file is a leaf, its children means offset in this file.

### Data Node

Use byte count method to isolate each data. Format is below.

```text
0    8   16   24   32   40   48   56  63
+--------------------------------------+
|                 size                 |
+--------------------------------------+
/                                      /
/                 data                 /
/                                      /
+--------------------------------------+
```

- `size`: The size of data.

## Steps
1. Translate codes in `db-cpp` to C.(done)
2. Add free list.
3. Cache.
4. Add new functions.
5. ...
