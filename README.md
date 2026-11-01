# Database Internals in C

Three core pieces of a database storage engine, written in C on top of a page-level buffer manager: a **heap file**, a **disk-resident B+ tree index**, and an **external merge sort**.

Nothing here touches a file directly. Every byte is read and written through fixed-size 512-byte blocks that have to be *pinned* in a buffer pool of only 100 blocks, marked dirty when modified, and *unpinned* when done. That's the constraint a real DBMS works under. It turns the classic data structures into page layouts: a B+ tree node isn't a struct joined to others by pointers but a block that refers to other blocks by number. Sorting a file that doesn't fit in memory becomes a matter of streaming runs through a handful of buffer pages.

```
 ┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────────┐
 │  01  Heap file       │  │  02  B+ tree index   │  │  03  External merge sort │
 │  unordered records,  │  │  search / insert /   │  │  sorted runs + b-way     │
 │  block chain         │  │  delete, rebalancing │  │  merge passes            │
 └──────────┬───────────┘  └──────────┬───────────┘  └────────────┬─────────────┘
            │                         │                  heap-file access layer
 ┌──────────┴─────────────────────────┴───────────────────────────┴─────────────┐
 │  BF: block-level buffer manager                                              │
 │  512-byte blocks · 100-block buffer pool · pin / unpin · dirty write-back    │
 │  LRU or MRU replacement                                                      │
 └──────────────────────────────────────────────────────────────────────────────┘
```

**Highlights**

- A B+ tree that lives entirely on disk, with search, insert and full **delete**. Delete handles borrowing from siblings, cascading merges, root growth and collapse, and a free list that recycles released pages. The tree survives being closed and reopened.
- An external merge sort that orders a file of any size using a fixed buffer budget: one sort phase, then **b-way merge passes** that cut the number of sorted runs by a factor of *b* each time.
- Test suites that don't take the API's word for it. They walk the on-disk pages directly and check every structural invariant against a reference model. For the B+ tree, everything is repeated under three node geometries and across close/reopen cycles.

---

## The storage layer

All three components are built on **BF**, a block-level buffer manager supplied by the course as a precompiled library ([`blockfile/`](blockfile/include/bf.h)). It exposes files as arrays of 512-byte blocks:

| Call | Effect |
|---|---|
| `BF_AllocateBlock` / `BF_GetBlock` | bring a block into the buffer pool and **pin** it |
| `BF_Block_GetData` | pointer to the block's 512 bytes inside the pool |
| `BF_Block_SetDirty` | the block changed and must be written back |
| `BF_UnpinBlock` | the caller is done; the block may now be evicted (LRU/MRU) |

The pool holds only 100 blocks, and pinned blocks can't be evicted. A file can't be closed while any of its blocks is still pinned. Every algorithm below is therefore written to hold only a few blocks at a time and to release each one as soon as it's no longer needed. The test suites enforce this: a leaked pin makes the final close fail.

---

## 01 · Heap file

[`01-heap-file/`](01-heap-file) · `hp_file.c`

The simplest organisation: records are appended in arrival order. It's the baseline that an index improves on.

```
block 0        HP_info header: first data block, last data block, block count
block 1..n     [ record | record | ... | record | free space | HP_block_info ]
                                                   trailer: count, free bytes, next block
```

- **Insert** is O(1). The header remembers the last block in the chain, so a record goes straight into its free space. When it fills up, a new block is allocated and linked in.
- **Lookup** (`HP_GetAllEntries`) has no faster path than a full scan of the chain. It returns the number of blocks it read, which makes the cost of not having an index directly visible. There's no primary key, so every match is returned.
- The header is cached in memory while the file is open and **written back to block 0 on close**, so the file reopens exactly as it was left.

---

## 02 · B+ tree index

[`02-bplus-tree/`](02-bplus-tree) · `bp_file.c`, `bp_datanode.c`, `bp_indexnode.c`

A B+ tree keyed on `Record.id`, stored as a graph of blocks. Every node occupies exactly one block and refers to its children by block number, so the whole structure lives in the file.

### Page layout

| Page | Contents | Capacity (512-byte block) |
|---|---|---|
| Header (block 0) | magic number, root block, height, record count, block count, free-list head | n/a |
| Data node (leaf) | type tag, record count, next-leaf block, records sorted by id | 8 records |
| Index node | type tag, key count, separator keys, child block ids | 62 keys / fan-out 63 |
| Free page | type tag, next free block | n/a |

Node capacities are derived from the block size at compile time and can be overridden (`-DBP_DATA_CAPACITY=3 -DBP_INDEX_CAPACITY=3` reproduces the order-4 tree of the original assignment). With a fan-out of 63 the tree stays shallow: four levels can index up to two million records, and every lookup reads exactly one block per level.

A small tree, as printed by `BP_PrintTree` (index keys in `[ ]`, leaf contents in `( )`):

```
B+ tree: height 2, 30 records, 7 blocks
  L0: [20 35 59 89]
  L1: (0 3 4 5 6 12) (20 23 24 27 30) (35 36 39 43 47 49 50 51) (59 60 68 72 73 76 88) (89 92 96 98)
```

### Operations

**Search** descends from the root using binary search inside each node. For a separator *k*, everything in the left subtree is `< k` and everything in the right is `>= k`.

**Insert** descends to the target leaf and records the path. If the leaf is full, its records plus the new one are split in two. The lower half stays in the existing block and only the upper half moves to a newly allocated one, so a split costs a single allocation. The new leaf's smallest key is pushed up to the parent. If the parent is full as well, it's first expanded into a one-larger temporary "big" node, then cut around its middle key, which moves up another level. A split that reaches the root grows the tree by one level.

**Delete** removes the record and restores the half-full invariant on the way back up:

1. If a sibling has a spare entry, **borrow** it and fix the separator between them (for index nodes this is a rotation through the parent).
2. Otherwise **merge** with a sibling and remove their separator from the parent, which may make the parent underflow in turn.
3. When the root index node loses its last key, its only child becomes the new root and the tree **shrinks by a level**.

Blocks released by merges are pushed onto a **free list** and reused by later splits. The file doesn't grow while freed pages are available: the test suite deletes every record, rebuilds the tree, and checks that the block count is unchanged.

### Design decisions

- **Block numbers, not pointers.** Nodes are viewed in place inside pinned buffer pages, never `malloc`'d, so the tree is exactly what's on disk and is reloaded by reading the header block.
- **A path stack instead of parent pointers.** The descent records which block and child slot it went through at each level, and splits and merges walk that path back up. Parent pointers stored on disk would mean rewriting every moved child's block on each split.
- **Minimum occupancy chosen so rebalancing can't overflow.** Leaves keep at least ⌈cap/2⌉ records and index nodes at least ⌊cap/2⌋ keys. A split always produces two legal nodes, and an underflowing node plus a sibling that can't lend always fit in one block.
- **Lazy separators.** Deleting a leaf's smallest key doesn't rewrite its ancestors. A separator only needs to be a valid bound, not an existing key, and that's the invariant the validator checks.
- **Few pins at a time.** An insert pins at most two blocks at once, and a delete at most four (node, parent and both siblings), however tall the tree is.

### API

```c
BF_Init(LRU);
BP_CreateFile("people.db");

int fd;
BPLUS_INFO* index = BP_OpenFile("people.db", &fd);

BP_InsertEntry(fd, index, record);            // returns the leaf block that received it, -1 on duplicate id

Record* hit = NULL;
if (BP_GetEntry(fd, index, 4217, &hit) == 0)  // exactly `height` block reads
    printRecord(*hit);

BP_DeleteEntry(fd, index, 4217);              // borrow / merge / shrink as needed

BP_CloseFile(fd, index);                      // persists the header; the tree reopens intact
BF_Close();
```

---

## 03 · External merge sort

[`03-external-sort/`](03-external-sort) · `chunk.c`, `sort.c`, `merge.c`

Sorts a heap file by `(surname, name)` when the file is far larger than the buffer pool, using the classic two-phase scheme.

**Chunks as views.** A `CHUNK` is a range of consecutive blocks, a view onto the file rather than a copy. `CHUNK_Iterator` walks a file chunk by chunk, and `CHUNK_RecordIterator` is a cursor over one chunk's records that touches one block at a time.

**Sort phase.** The file is cut into chunks of *k* blocks, each small enough for the buffer pool. Each chunk is read, sorted in memory and written back in place. The result is a sequence of sorted runs.

**Merge passes.** Runs are taken *b* at a time. Each group gets one cursor per run, and the merge repeatedly emits the smallest head record to the output file. A pass therefore needs about *b* + 1 buffer pages however large the file is, reads and writes every block exactly once, and divides the number of runs by *b*. Sorting *N* blocks takes ⌈log_b(N / k)⌉ passes. Ties go to the earlier run, so the merge is stable.

From the included driver (11,520 records, 1,280 blocks, 5-block chunks, 4-way merge):

```
File ./test2.db has 1280 blocks
After the sort phase, file ./test2.db has 256 chunk(s)
After the merge phase 1, the output file ./test2.db1.db has 64 chunk(s)
After the merge phase 2, the output file ./test2.db2.db has 16 chunk(s)
After the merge phase 3, the output file ./test2.db3.db has 4 chunk(s)
After the merge phase 4, the output file ./test2.db4.db has 1 chunk(s)
```

This component reads and writes records through the course's heap-file access library (`03-external-sort/lib/libhp_file.so`), which pins a block on every record access. The chunk layer pairs each access with an unpin, so a cursor stepping through a block is served from the buffer pool rather than the disk.

---

## Testing

```bash
make test
```

| Suite | What it verifies |
|---|---|
| `01-heap-file/tests/hp_test.c` | Walks the block chain directly: allocation order, per-block counters and free space, records stored in insertion order, header/file block counts agree, a lookup scans exactly the data blocks; then close → reopen → append → reopen. |
| `02-bplus-tree/tests/bp_test.c` | After every phase, an independent validator reads every block and checks node types, sorted keys, separator bounds for every record, minimum fill of every non-root node, uniform leaf depth, a leaf chain that visits exactly the leaves in order, and that every block is either the header, a reachable node, or on the free list. Scenarios: ascending, descending and random inserts with duplicates; deleting everything and rebuilding from the free list; a 100,000-operation mixed insert/delete workload against a reference model; persistence across three open/close sessions. **The whole suite runs under three node geometries** (8/63, the original order 4, and the minimal order 3), which pushes the same code through trees up to 12 levels deep. |
| `03-external-sort/tests/sort_test.c` | For ten configurations (single record, partial last blocks, fan-ins that don't divide the run count, up to 30,000 records with a 16-way merge), checks after the sort phase and after **every** merge pass that each run is sorted, the run count is right, and the file holds exactly the input's records: same count, same order-independent fingerprint. |

The B+ tree suite is also clean under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Building and running

**Requirements:** Linux x86-64, `gcc`, `make`. The course libraries (`libbf.so`, `libhp_file.so`) are prebuilt x86-64 Linux binaries.

```bash
make          # build all three components
make test     # run every test suite
make demo     # walkthroughs: block layer, heap-file lookups, B+ tree lifecycle, sort passes
make clean
```

Each component also builds on its own (`make -C 02-bplus-tree test`). Binaries are placed in, and run from, each component's `build/` directory, so the `.db` files they create stay out of the source tree.

On macOS or another architecture, use Docker:

```bash
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src gcc:13 make test
```

## Repository layout

```
.
├── blockfile/              BF buffer manager: bf.h + prebuilt libbf.so (provided)
├── 01-heap-file/
│   ├── include/            hp_file.h, record.h
│   ├── src/                hp_file.c, record.c
│   ├── examples/           bf_main.c (block-layer tour), hp_main.c (insert + lookup driver)
│   └── tests/              hp_test.c
├── 02-bplus-tree/
│   ├── include/            bp_file.h (API + header), bp_datanode.h, bp_indexnode.h, record.h
│   ├── src/                bp_file.c (search/insert/delete), bp_datanode.c, bp_indexnode.c, record.c
│   ├── examples/           bp_main.c (build, reopen, delete walkthrough)
│   └── tests/              bp_test.c (validator + scenarios)
├── 03-external-sort/
│   ├── include/            chunk.h, sort.h, merge.h, hp_file.h, record.h
│   ├── lib/                libhp_file.so (provided heap-file access library)
│   ├── src/                chunk.c, sort.c, merge.c, record.c
│   ├── examples/           sort_main.c (sort + merge passes, run counts per pass)
│   └── tests/              sort_test.c
├── common.mk               shared compiler / linker settings
└── Makefile                builds and tests everything
```

## Origin

Built for the *Implementation of Database Systems* course (Department of Informatics & Telecommunications, University of Athens, 2024–25). The course provided the BF buffer manager, the heap-file library used by the sort, the record generators and the driver programs' skeletons. The heap file, B+ tree, chunk layer, sort and merge are implemented here.
