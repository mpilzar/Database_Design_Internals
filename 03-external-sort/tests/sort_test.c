/*
 * External merge sort test suite.
 *
 * For a range of (records, chunk size, b) configurations, including files
 * whose last block is only partially filled and fan-ins that do not divide
 * the number of runs, it checks after the sort phase and after every merge
 * pass that:
 *
 *   - the file holds exactly the same multiset of records as the input
 *     (count + order-independent fingerprint), so nothing is lost or duplicated
 *   - every run of chunkSize * b^pass blocks is sorted by (surname, name)
 *   - the number of runs is ceil(blocks / run width)
 *
 * and that the final file is one fully sorted run.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "merge.h"

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

typedef struct {
  long count;
  uint64_t fingerprint;
} Summary;

static uint64_t hash_record(const Record* r) {
  uint64_t h = 1469598103934665603ULL;
  const char* fields[3] = { r->name, r->surname, r->city };
  for (int f = 0; f < 3; f++)
    for (const char* c = fields[f]; *c; c++) h = (h ^ (unsigned char)*c) * 1099511628211ULL;
  h = (h ^ (uint64_t)(uint32_t)r->id) * 1099511628211ULL;
  return h;
}

static int compare(const Record* a, const Record* b) {
  int c = strcmp(a->surname, b->surname);
  return c ? c : strcmp(a->name, b->name);
}

/* Scans the whole file: verifies that each run of `runBlocks` blocks is sorted
 * and returns the record count and fingerprint. */
static Summary scan(int fd, int runBlocks) {
  Summary s = { 0, 0 };
  int last = HP_GetIdOfLastBlock(fd);
  Record prev;
  int havePrev = 0;
  for (int b = 1; b <= last; b++) {
    if ((b - 1) % runBlocks == 0) havePrev = 0;      /* a new run starts here */
    int n = HP_GetRecordCounter(fd, b);
    for (int i = 0; i < n; i++) {
      Record r;
      CHECK(HP_GetRecord(fd, b, i, &r) != -1, "cannot read block %d slot %d", b, i);
      HP_Unpin(fd, b);
      if (havePrev)
        CHECK(compare(&prev, &r) <= 0, "run starting near block %d is not sorted", b);
      prev = r;
      havePrev = 1;
      s.count++;
      s.fingerprint += hash_record(&r);
    }
  }
  return s;
}

static void run_case(int totalRecords, int chunkSize, int bWay) {
  const char* base = "sort_test.db";
  char name[64];
  remove(base);
  for (int i = 1; i < 64; i++) { sprintf(name, "%s%d.db", base, i); remove(name); }

  CHECK(HP_CreateFile((char*)base) == 0, "create");
  int fd;
  CHECK(HP_OpenFile((char*)base, &fd) == 0, "open");
  srand(totalRecords * 31 + chunkSize * 7 + bWay);
  Summary input = { 0, 0 };
  for (int i = 0; i < totalRecords; i++) {
    Record r = randomRecord();
    HP_InsertEntry(fd, r);
    input.count++;
    input.fingerprint += hash_record(&r);
  }
  int blocks = HP_GetIdOfLastBlock(fd);

  /* Sort phase. */
  sort_FileInChunks(fd, chunkSize);
  int width = chunkSize;
  Summary s = scan(fd, width);
  CHECK(s.count == input.count && s.fingerprint == input.fingerprint, "sort phase changed the records");

  /* Merge passes. */
  int passes = 0;
  while (width < blocks) {
    sprintf(name, "%s%d.db", base, ++passes);
    CHECK(HP_CreateFile(name) == 0, "create output");
    int out;
    CHECK(HP_OpenFile(name, &out) == 0, "open output");
    merge(fd, width, bWay, out);
    CHECK(HP_CloseFile(fd) == 0, "closing pass input failed (blocks left pinned?)");
    fd = out;
    width *= bWay;

    CHECK(HP_GetIdOfLastBlock(fd) == blocks, "pass %d changed the block count", passes);
    s = scan(fd, width);
    CHECK(s.count == input.count, "pass %d: %ld records, expected %ld", passes, s.count, input.count);
    CHECK(s.fingerprint == input.fingerprint, "pass %d altered the record multiset", passes);
  }

  /* One fully sorted run at the end. */
  s = scan(fd, blocks);
  CHECK(s.count == input.count && s.fingerprint == input.fingerprint, "final file differs from input");
  CHECK(HP_CloseFile(fd) == 0, "closing final file failed (blocks left pinned?)");

  printf("  %6d records, %4d blocks, chunk %2d, %d-way: %2d merge pass(es) ok\n",
         totalRecords, blocks, chunkSize, bWay, passes);

  remove(base);
  for (int i = 1; i <= passes; i++) { sprintf(name, "%s%d.db", base, i); remove(name); }
}

int main(void) {
  BF_Init(LRU);
  printf("External merge sort tests\n");
  run_case(1, 1, 2);          /* single record */
  run_case(9, 1, 2);          /* single full block */
  run_case(37, 1, 2);         /* partial last block */
  run_case(100, 1, 3);
  run_case(1000, 3, 3);
  run_case(2000, 10, 2);
  run_case(4608, 1, 2);       /* the course's test1 configuration */
  run_case(5003, 2, 7);       /* fan-in that does not divide the run count */
  run_case(11520, 5, 4);      /* the course's test2 configuration */
  run_case(30000, 4, 16);
  BF_Close();
  printf("All tests passed (%d checks).\n", checks);
  return 0;
}
