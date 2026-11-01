/*
 * Heap file test suite: checks the on-disk layout directly through the BF
 * layer (block chain, per-block counters, record order) and checks that the
 * file survives close / reopen and keeps growing correctly afterwards.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"

#define FILE_NAME "hp_test.db"
#define PER_BLOCK ((int)((BF_BLOCK_SIZE - sizeof(HP_block_info)) / sizeof(Record)))

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

/* Walks the block chain and checks it holds exactly `expected`, in order. */
static void verify_chain(int fd, HP_info* info, const Record* expected, int n) {
  BF_Block* block;
  BF_Block_Init(&block);
  int seen = 0, blocks = 0, prev = 0;
  for (int id = info->first_block_rec; id != -1; ) {
    CHECK(id > prev, "chain is not in allocation order (%d after %d)", id, prev);
    CHECK(BF_GetBlock(fd, id, block) == BF_OK, "read block %d", id);
    char* data = BF_Block_GetData(block);
    HP_block_info* bi = (HP_block_info*)(data + BF_BLOCK_SIZE - sizeof(HP_block_info));
    int next = bi->next_block_id;
    CHECK(bi->record_count >= 1 && bi->record_count <= PER_BLOCK, "block %d holds %d records", id, bi->record_count);
    CHECK(next == -1 || bi->record_count == PER_BLOCK, "non-final block %d is not full", id);
    CHECK(bi->current_block_capacity == (int)(BF_BLOCK_SIZE - sizeof(HP_block_info)) - bi->record_count * (int)sizeof(Record),
          "block %d free-space counter is wrong", id);
    for (int i = 0; i < bi->record_count; i++, seen++) {
      Record* r = (Record*)(data + i * sizeof(Record));
      CHECK(seen < n && r->id == expected[seen].id && strcmp(r->name, expected[seen].name) == 0,
            "record %d differs", seen);
    }
    CHECK(BF_UnpinBlock(block) == BF_OK, "unpin");
    prev = id;
    id = next;
    blocks++;
  }
  BF_Block_Destroy(&block);
  CHECK(seen == n, "chain holds %d records, expected %d", seen, n);
  CHECK(n == 0 || prev == info->last_block_id, "last_block_id is stale");
  CHECK(info->number_of_blocks == blocks + 1, "header counts %d blocks, chain has %d + header", info->number_of_blocks, blocks);
  int fileBlocks;
  BF_GetBlockCounter(fd, &fileBlocks);
  CHECK(fileBlocks == info->number_of_blocks, "file has %d blocks, header says %d", fileBlocks, info->number_of_blocks);
}

int main(void) {
  const int n = 1000, more = 333;
  Record* recs = calloc(n + more, sizeof(Record));
  int fd;

  printf("Heap file tests (%d records per block)\n", PER_BLOCK);
  remove(FILE_NAME);
  BF_Init(LRU);
  CHECK(HP_CreateFile(FILE_NAME) == 0, "create");
  HP_info* info = HP_OpenFile(FILE_NAME, &fd);
  CHECK(info != NULL, "open");
  CHECK(HP_GetAllEntries(fd, info, 0) == 0, "scan of an empty file read blocks");
  verify_chain(fd, info, recs, 0);
  printf("  %-40s ok\n", "empty file");

  int lastBlock = 0;
  for (int i = 0; i < n; i++) {
    recs[i] = randomRecord();
    if (i % 250 == 0) recs[i].id = 777777;                   /* no primary key: duplicates allowed */
    int b = HP_InsertEntry(fd, info, recs[i]);
    CHECK(b == lastBlock || b == lastBlock + 1, "insert %d went to block %d after %d", i, b, lastBlock);
    CHECK(b == 1 + i / PER_BLOCK, "insert %d went to block %d", i, b);
    lastBlock = b;
  }
  verify_chain(fd, info, recs, n);
  printf("  %-40s ok  (%d blocks)\n", "inserts fill blocks in order", info->number_of_blocks);

  printf("  scanning for duplicated id 777777 (expect 4 matches):\n");
  int scanned = HP_GetAllEntries(fd, info, 777777);
  CHECK(scanned == info->number_of_blocks - 1, "scan read %d blocks", scanned);
  CHECK(HP_CloseFile(fd, info) == 0, "close");
  BF_Close();

  BF_Init(MRU);
  info = HP_OpenFile(FILE_NAME, &fd);
  CHECK(info != NULL, "reopen");
  verify_chain(fd, info, recs, n);
  for (int i = n; i < n + more; i++) {
    recs[i] = randomRecord();
    CHECK(HP_InsertEntry(fd, info, recs[i]) == 1 + i / PER_BLOCK, "append after reopen");
  }
  verify_chain(fd, info, recs, n + more);
  CHECK(HP_CloseFile(fd, info) == 0, "close");
  BF_Close();

  BF_Init(LRU);
  info = HP_OpenFile(FILE_NAME, &fd);
  verify_chain(fd, info, recs, n + more);
  CHECK(HP_CloseFile(fd, info) == 0, "close");
  BF_Close();
  printf("  %-40s ok\n", "close / reopen / append persistence");

  remove(FILE_NAME);
  free(recs);
  printf("All tests passed (%d checks).\n", checks);
  return 0;
}
