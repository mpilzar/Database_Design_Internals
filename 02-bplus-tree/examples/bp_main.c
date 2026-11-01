/*
 * B+ tree walkthrough.
 *
 *   1. Builds a small tree and prints it, to show its shape.
 *   2. Inserts N records with random ids, then looks up 10 sampled ids.
 *   3. Closes the file, shuts the buffer manager down, reopens and repeats
 *      the lookups: the tree is read back from disk.
 *   4. Deletes most of the records and shows the tree contracting.
 *
 * Usage: ./bp_main [number_of_records]   (default 10000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "bp_file.h"
#include "bp_datanode.h"
#include "bp_indexnode.h"

#define FILE_NAME "data.db"

static void lookup(int fd, BPLUS_INFO* info, int id) {
  Record buffer;
  Record* result = &buffer;
  if (BP_GetEntry(fd, info, id, &result) == 0)
    printf("    %6d -> found   ", id), printRecord(*result);
  else
    printf("    %6d -> not found\n", id);
}

static void stats(const char* label, BPLUS_INFO* info) {
  printf("  %-34s height %d, %6d records, %5d blocks\n",
         label, info->height, info->numberOfRecords, info->numberOfBlocks);
}

int main(int argc, char* argv[]) {
  int n = argc > 1 ? atoi(argv[1]) : 10000;
  if (n < 100) n = 100;
  srand(42);
  int fd;

  printf("Node geometry: %d records per data node, %d keys / %d children per index node\n\n",
         BP_DATA_CAPACITY, BP_INDEX_CAPACITY, BP_INDEX_CAPACITY + 1);

  /* 1. A tree small enough to print. */
  BF_Init(LRU);
  remove(FILE_NAME);
  BP_CreateFile(FILE_NAME);
  BPLUS_INFO* info = BP_OpenFile(FILE_NAME, &fd);
  for (int i = 0; i < 40; i++) {
    Record r = randomRecord();
    r.id = rand() % 100;
    BP_InsertEntry(fd, info, r);
  }
  printf("1. A small tree (index keys in [ ], data node ids in ( )):\n");
  BP_PrintTree(fd, info);
  BP_CloseFile(fd, info);
  BF_Close();

  /* 2. A bigger tree. */
  printf("\n2. Inserting %d records with random ids\n", n);
  BF_Init(LRU);
  remove(FILE_NAME);
  BP_CreateFile(FILE_NAME);
  info = BP_OpenFile(FILE_NAME, &fd);

  int* ids = malloc(n * sizeof(int));
  int inserted = 0;
  for (int i = 0; i < n; i++) {
    Record r = randomRecord();
    r.id = rand() % (n * 10);
    if (BP_InsertEntry(fd, info, r) != -1) ids[inserted++] = r.id;
  }
  stats("after inserts:", info);
  printf("  (%d duplicate ids rejected)\n", n - inserted);

  int sample[10];
  for (int i = 0; i < 10; i++) sample[i] = ids[rand() % inserted];
  printf("  lookups:\n");
  for (int i = 0; i < 10; i++) lookup(fd, info, sample[i]);
  BP_CloseFile(fd, info);
  BF_Close();

  /* 3. Everything comes back from disk. */
  printf("\n3. Closed the file and the buffer manager; reopening\n");
  BF_Init(LRU);
  info = BP_OpenFile(FILE_NAME, &fd);
  stats("after reopen:", info);
  printf("  lookups:\n");
  for (int i = 0; i < 10; i++) lookup(fd, info, sample[i]);

  /* 4. Deletes: remove ~98% of the records, sparing the first five sampled ids. */
  printf("\n4. Deleting ~98%% of the records (keeping the first 5 sampled ids)\n");
  int deleted = 0;
  for (int i = 0; i < inserted; i++) {
    int keep = i % 50 == 0;
    for (int s = 0; s < 5; s++) keep |= ids[i] == sample[s];
    if (!keep && BP_DeleteEntry(fd, info, ids[i]) == 0) deleted++;
  }
  printf("  %d records deleted\n", deleted);
  stats("after deletes:", info);
  printf("  (blocks released by merges sit on a free list, ready for reuse)\n");
  printf("  lookups:\n");
  for (int i = 0; i < 10; i++) lookup(fd, info, sample[i]);

  BP_CloseFile(fd, info);
  BF_Close();
  free(ids);
  return 0;
}
