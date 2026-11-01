/*
 * B+ tree test suite.
 *
 * Every scenario is checked against an in-memory reference set, and after
 * each phase the whole file is walked block by block by an independent
 * validator that checks the structural invariants:
 *
 *   - node type tags are correct and every leaf is at depth == height
 *   - keys are strictly increasing inside every node
 *   - every record lies inside the key range its ancestors' separators allow
 *   - every non-root node is at least half full (BP_DATA_MIN / BP_INDEX_MIN)
 *   - the leaf chain visits exactly the leaves, left to right, and ends in -1
 *   - header counters match reality, and every block is either the header,
 *     a reachable node, or on the free list
 *   - no blocks are left pinned (BP_CloseFile would fail otherwise)
 *
 * Build with different -DBP_DATA_CAPACITY / -DBP_INDEX_CAPACITY values to
 * exercise deep trees (see the Makefile's `test` target).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "bp_file.h"
#include "bp_datanode.h"
#include "bp_indexnode.h"

#define FILE_NAME "bp_test.db"

static int checks = 0;

#define CHECK(cond, ...)                                              \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
      fprintf(stderr, __VA_ARGS__);                                   \
      fprintf(stderr, "\n");                                          \
      exit(1);                                                        \
    }                                                                 \
  } while (0)

/* ------------------------------------------------------------------------ */
/* Reference model                                                          */
/* ------------------------------------------------------------------------ */

typedef struct {
  int keyspace;
  char* present;
  Record* records;
  int count;
} Reference;

static Reference ref_new(int keyspace) {
  Reference r = { keyspace, calloc(keyspace, 1), calloc(keyspace, sizeof(Record)), 0 };
  return r;
}

static void ref_free(Reference* r) { free(r->present); free(r->records); }

static Record make_record(int id) {
  Record rec = randomRecord();
  rec.id = id;
  return rec;
}

static int record_equal(const Record* a, const Record* b) {
  return a->id == b->id && strcmp(a->name, b->name) == 0 &&
         strcmp(a->surname, b->surname) == 0 && strcmp(a->city, b->city) == 0;
}

/* ------------------------------------------------------------------------ */
/* Structural validator                                                     */
/* ------------------------------------------------------------------------ */

typedef struct {
  int fd;
  int height;
  int records;
  int nodes;
  int* leaves;          /* leaf block ids in key order */
  int leafCount;
  int leafCapacity;
  int* leafNext;        /* nextNode of each leaf, same order */
  const Reference* ref;
} Walk;

static void read_block(int fd, int id, void* out) {
  BF_Block* block;
  BF_Block_Init(&block);
  CHECK(BF_GetBlock(fd, id, block) == BF_OK, "cannot read block %d", id);
  memcpy(out, BF_Block_GetData(block), BF_BLOCK_SIZE);
  CHECK(BF_UnpinBlock(block) == BF_OK, "cannot unpin block %d", id);
  BF_Block_Destroy(&block);
}

/* Checks the subtree rooted at block `id`; all of its ids must be in [lo, hi). */
static void walk(Walk* w, int id, int depth, long lo, long hi, int isRoot) {
  char buf[BF_BLOCK_SIZE];
  read_block(w->fd, id, buf);
  w->nodes++;

  if (depth < w->height) {
    BPLUS_INDEX_NODE* node = (BPLUS_INDEX_NODE*)buf;
    CHECK(node->type == BP_INDEX_NODE, "block %d at depth %d should be an index node", id, depth);
    CHECK(node->numberOfKeys <= BP_INDEX_CAPACITY, "index node %d overfull", id);
    CHECK(node->numberOfKeys >= (isRoot ? 1 : BP_INDEX_MIN),
          "index node %d underfull (%d keys)", id, node->numberOfKeys);
    for (int i = 0; i < node->numberOfKeys; i++) {
      CHECK(node->keys[i] >= lo && node->keys[i] <= hi, "index node %d key out of range", id);
      if (i) CHECK(node->keys[i - 1] < node->keys[i], "index node %d keys not increasing", id);
    }
    BPLUS_INDEX_NODE copy = *node;
    for (int i = 0; i <= copy.numberOfKeys; i++) {
      long clo = i == 0 ? lo : copy.keys[i - 1];
      long chi = i == copy.numberOfKeys ? hi : copy.keys[i];
      walk(w, copy.children[i], depth + 1, clo, chi, 0);
    }
    return;
  }

  BPLUS_DATA_NODE* leaf = (BPLUS_DATA_NODE*)buf;
  CHECK(depth == w->height, "leaf %d at depth %d, height is %d", id, depth, w->height);
  CHECK(leaf->type == BP_DATA_NODE, "block %d should be a data node", id);
  CHECK(leaf->numberOfRecords <= BP_DATA_CAPACITY, "data node %d overfull", id);
  CHECK(leaf->numberOfRecords >= (isRoot ? 1 : BP_DATA_MIN),
        "data node %d underfull (%d records)", id, leaf->numberOfRecords);
  for (int i = 0; i < leaf->numberOfRecords; i++) {
    Record* r = &leaf->storedRecords[i];
    CHECK(r->id >= lo && r->id < hi, "record %d in leaf %d outside [%ld, %ld)", r->id, id, lo, hi);
    if (i) CHECK(leaf->storedRecords[i - 1].id < r->id, "leaf %d not sorted", id);
    if (w->ref) {
      CHECK(r->id >= 0 && r->id < w->ref->keyspace && w->ref->present[r->id],
            "record %d is in the tree but should not be", r->id);
      CHECK(record_equal(r, &w->ref->records[r->id]),
            "record %d payload corrupted", r->id);
    }
  }
  w->records += leaf->numberOfRecords;

  if (w->leafCount == w->leafCapacity) {
    w->leafCapacity = w->leafCapacity ? 2 * w->leafCapacity : 64;
    w->leaves = realloc(w->leaves, w->leafCapacity * sizeof(int));
    w->leafNext = realloc(w->leafNext, w->leafCapacity * sizeof(int));
  }
  w->leaves[w->leafCount] = id;
  w->leafNext[w->leafCount] = leaf->nextNode;
  w->leafCount++;
}

static void validate(int fd, BPLUS_INFO* info, const Reference* ref) {
  int blocks;
  CHECK(BF_GetBlockCounter(fd, &blocks) == BF_OK, "block counter");
  CHECK(info->numberOfBlocks == blocks, "header says %d blocks, file has %d", info->numberOfBlocks, blocks);

  Walk w = { fd, info->height, 0, 0, NULL, 0, 0, NULL, ref };
  if (info->root == -1) {
    CHECK(info->height == 0 && info->numberOfRecords == 0, "empty tree has stale header");
  } else {
    CHECK(info->height >= 1, "non-empty tree with height %d", info->height);
    walk(&w, info->root, 1, -2147483648L, 2147483648L, 1);
  }

  CHECK(w.records == info->numberOfRecords, "tree holds %d records, header says %d", w.records, info->numberOfRecords);
  if (ref) CHECK(w.records == ref->count, "tree holds %d records, expected %d", w.records, ref->count);

  for (int i = 0; i < w.leafCount; i++) {
    int expected = i + 1 < w.leafCount ? w.leaves[i + 1] : -1;
    CHECK(w.leafNext[i] == expected, "leaf chain broken after leaf %d", w.leaves[i]);
  }

  int freeBlocks = 0;
  for (int id = info->freeList; id != -1; ) {
    int buf[BF_BLOCK_SIZE / sizeof(int)];
    read_block(fd, id, buf);
    CHECK(buf[0] == BP_FREE_NODE, "block %d on the free list is not free", id);
    CHECK(++freeBlocks <= blocks, "free list has a cycle");
    id = buf[1];
  }
  CHECK(1 + w.nodes + freeBlocks == blocks,
        "block accounting: 1 header + %d nodes + %d free != %d", w.nodes, freeBlocks, blocks);

  free(w.leaves);
  free(w.leafNext);
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static BPLUS_INFO* fresh_tree(int* fd) {
  remove(FILE_NAME);
  CHECK(BP_CreateFile(FILE_NAME) == 0, "BP_CreateFile");
  BPLUS_INFO* info = BP_OpenFile(FILE_NAME, fd);
  CHECK(info != NULL, "BP_OpenFile");
  return info;
}

static void do_insert(int fd, BPLUS_INFO* info, Reference* ref, int id) {
  Record rec = make_record(id);
  int block = BP_InsertEntry(fd, info, rec);
  if (ref->present[id]) {
    CHECK(block == -1, "duplicate id %d was accepted", id);
    return;
  }
  CHECK(block > 0, "insert of %d failed", id);

  /* The returned block must be the data node now holding the record. */
  BPLUS_DATA_NODE leaf;
  char buf[BF_BLOCK_SIZE];
  read_block(fd, block, buf);
  memcpy(&leaf, buf, sizeof(leaf));
  CHECK(leaf.type == BP_DATA_NODE && datanode_find(&leaf, id) >= 0,
        "insert of %d returned block %d, which does not hold it", id, block);

  ref->present[id] = 1;
  ref->records[id] = rec;
  ref->count++;
}

static void do_delete(int fd, BPLUS_INFO* info, Reference* ref, int id) {
  int rc = BP_DeleteEntry(fd, info, id);
  if (!ref->present[id]) {
    CHECK(rc == -1, "deleting absent id %d reported success", id);
    return;
  }
  CHECK(rc == 0, "delete of %d failed", id);
  ref->present[id] = 0;
  ref->count--;
}

static void check_lookups(int fd, BPLUS_INFO* info, const Reference* ref) {
  for (int id = 0; id < ref->keyspace; id++) {
    Record buffer;
    Record* result = &buffer;
    int rc = BP_GetEntry(fd, info, id, &result);
    if (ref->present[id]) {
      CHECK(rc == 0 && result != NULL, "lookup of %d missed", id);
      CHECK(record_equal(result, &ref->records[id]), "lookup of %d returned wrong record", id);
    } else {
      CHECK(rc == -1 && result == NULL, "lookup of absent %d hit", id);
    }
  }
}

static void shuffle(int* a, int n) {
  for (int i = n - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int t = a[i]; a[i] = a[j]; a[j] = t;
  }
}

static void close_tree(int fd, BPLUS_INFO* info) {
  CHECK(BP_CloseFile(fd, info) == 0, "BP_CloseFile failed (blocks left pinned?)");
}

/* ------------------------------------------------------------------------ */
/* Scenarios                                                                */
/* ------------------------------------------------------------------------ */

static void test_ordered_inserts(int n, int ascending) {
  BF_Init(LRU);
  int fd;
  BPLUS_INFO* info = fresh_tree(&fd);
  Reference ref = ref_new(n);
  for (int i = 0; i < n; i++)
    do_insert(fd, info, &ref, ascending ? i : n - 1 - i);
  validate(fd, info, &ref);
  check_lookups(fd, info, &ref);
  printf("  %-44s ok  (height %d, %d blocks)\n",
         ascending ? "ascending inserts" : "descending inserts", info->height, info->numberOfBlocks);
  close_tree(fd, info);
  BF_Close();
  ref_free(&ref);
}

static void test_random_with_duplicates(int n) {
  BF_Init(LRU);
  int fd;
  BPLUS_INFO* info = fresh_tree(&fd);
  Reference ref = ref_new(n);
  for (int i = 0; i < n; i++)
    do_insert(fd, info, &ref, rand() % n);         /* plenty of duplicates */
  validate(fd, info, &ref);
  check_lookups(fd, info, &ref);
  printf("  %-44s ok  (%d unique of %d)\n", "random inserts with duplicates", ref.count, n);
  close_tree(fd, info);
  BF_Close();
  ref_free(&ref);
}

static void test_delete_everything(int n) {
  BF_Init(LRU);
  int fd;
  BPLUS_INFO* info = fresh_tree(&fd);
  Reference ref = ref_new(n);
  int* keys = malloc(n * sizeof(int));
  int* order = malloc(n * sizeof(int));
  for (int i = 0; i < n; i++) keys[i] = order[i] = i;

  shuffle(keys, n);
  memcpy(order, keys, n * sizeof(int));
  for (int i = 0; i < n; i++) do_insert(fd, info, &ref, keys[i]);
  int peakBlocks = info->numberOfBlocks;
  int peakHeight = info->height;

  shuffle(keys, n);
  for (int i = 0; i < n; i++) {
    do_delete(fd, info, &ref, keys[i]);
    if (i % (n / 8) == 0) validate(fd, info, &ref);
  }
  validate(fd, info, &ref);
  CHECK(info->root == -1 && info->height == 0, "tree not empty after deleting everything");
  do_delete(fd, info, &ref, 0);                    /* delete from an empty tree */

  /* Rebuilding the same tree (same insertion order, so the same number of
   * nodes) must be served entirely from the free list. */
  for (int i = 0; i < n; i++) do_insert(fd, info, &ref, order[i]);
  validate(fd, info, &ref);
  CHECK(info->numberOfBlocks == peakBlocks,
        "file grew from %d to %d blocks although freed blocks were available", peakBlocks, info->numberOfBlocks);

  printf("  %-44s ok  (height %d -> 0 -> %d, file stayed at %d blocks)\n",
         "delete everything, then rebuild", peakHeight, info->height, peakBlocks);
  close_tree(fd, info);
  BF_Close();
  free(keys);
  free(order);
  ref_free(&ref);
}

static void test_mixed_workload(int keyspace, int operations) {
  BF_Init(LRU);
  int fd;
  BPLUS_INFO* info = fresh_tree(&fd);
  Reference ref = ref_new(keyspace);
  int inserts = 0, deletes = 0, maxHeight = 0;

  for (int op = 0; op < operations; op++) {
    int id = rand() % keyspace;
    /* Bias shifts over time so the tree both grows and shrinks substantially. */
    int insertPercent = (op / (operations / 4)) % 2 == 0 ? 70 : 30;
    if (rand() % 100 < insertPercent) { do_insert(fd, info, &ref, id); inserts++; }
    else { do_delete(fd, info, &ref, id); deletes++; }
    if (info->height > maxHeight) maxHeight = info->height;
    if (op % (operations / 20) == 0) validate(fd, info, &ref);
  }
  validate(fd, info, &ref);
  check_lookups(fd, info, &ref);
  printf("  %-44s ok  (%d ops, max height %d)\n", "mixed insert/delete workload", operations, maxHeight);
  close_tree(fd, info);
  BF_Close();
  ref_free(&ref);
}

static void test_persistence(int n) {
  Reference ref = ref_new(n);
  int fd;

  /* Session 1: build and partially delete. */
  BF_Init(LRU);
  BPLUS_INFO* info = fresh_tree(&fd);
  for (int i = 0; i < n; i++) do_insert(fd, info, &ref, rand() % n);
  for (int i = 0; i < n / 3; i++) do_delete(fd, info, &ref, rand() % n);
  int height = info->height;
  close_tree(fd, info);
  BF_Close();

  /* Session 2: everything must come back from disk. */
  BF_Init(MRU);
  info = BP_OpenFile(FILE_NAME, &fd);
  CHECK(info != NULL, "reopen failed");
  CHECK(info->height == height, "height not persisted");
  validate(fd, info, &ref);
  check_lookups(fd, info, &ref);
  for (int i = 0; i < n / 2; i++) do_delete(fd, info, &ref, rand() % n);
  for (int i = 0; i < n / 2; i++) do_insert(fd, info, &ref, rand() % n);
  close_tree(fd, info);
  BF_Close();

  /* Session 3: and once more after modifying a reopened file. */
  BF_Init(LRU);
  info = BP_OpenFile(FILE_NAME, &fd);
  CHECK(info != NULL, "second reopen failed");
  validate(fd, info, &ref);
  check_lookups(fd, info, &ref);
  printf("  %-44s ok  (%d records across 3 sessions)\n", "close / reopen persistence", ref.count);
  close_tree(fd, info);
  BF_Close();
  ref_free(&ref);
}

static void test_not_a_tree(void) {
  BF_Init(LRU);
  remove("not_a_tree.db");
  int fd;
  CHECK(BF_CreateFile("not_a_tree.db") == BF_OK, "create plain file");
  CHECK(BF_OpenFile("not_a_tree.db", &fd) == BF_OK, "open plain file");
  BF_Block* block;
  BF_Block_Init(&block);
  CHECK(BF_AllocateBlock(fd, block) == BF_OK, "allocate");
  memset(BF_Block_GetData(block), 0, BF_BLOCK_SIZE);
  BF_Block_SetDirty(block);
  BF_UnpinBlock(block);
  BF_Block_Destroy(&block);
  BF_CloseFile(fd);
  CHECK(BP_OpenFile("not_a_tree.db", &fd) == NULL, "opened a file that is not a B+ tree");
  printf("  %-44s ok\n", "rejects files that are not B+ trees");
  BF_Close();
  remove("not_a_tree.db");
}

int main(int argc, char* argv[]) {
  int scale = argc > 1 ? atoi(argv[1]) : 20000;
  srand(20241025);

  printf("B+ tree tests: data node capacity %d (min %d), index node capacity %d (min %d)\n",
         BP_DATA_CAPACITY, BP_DATA_MIN, BP_INDEX_CAPACITY, BP_INDEX_MIN);
  test_ordered_inserts(scale, 1);
  test_ordered_inserts(scale, 0);
  test_random_with_duplicates(scale);
  test_delete_everything(scale);
  test_mixed_workload(scale / 2, scale * 5);
  test_persistence(scale);
  test_not_a_tree();
  remove(FILE_NAME);
  printf("All tests passed (%d checks).\n", checks);
  return 0;
}
