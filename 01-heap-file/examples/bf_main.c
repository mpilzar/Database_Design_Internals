/*
 * Minimal tour of the BF block layer: allocate, write, persist, re-read.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "record.h"

#define CALL_OR_DIE(call)     \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      exit(code);             \
    }                         \
  }

int main() {
  int fd1;
  BF_Block *block;
  BF_Block_Init(&block);



  /* Part 1: create a file of 100 blocks, bring each one into the buffer pool
   * and write two records at its start. Each block is marked dirty so the
   * change reaches disk, and unpinned so the pool does not fill up. */
  remove("block_example.db");
  CALL_OR_DIE(BF_Init(LRU));
  CALL_OR_DIE(BF_CreateFile("block_example.db"))
  CALL_OR_DIE(BF_OpenFile("block_example.db", &fd1));

  void* data;
  for (int i = 0; i < 100; ++i) {
    CALL_OR_DIE(BF_AllocateBlock(fd1, block));  // append a new block (returned pinned)
    data = BF_Block_GetData(block);             // the block's bytes inside the buffer pool
    Record* rec = data;                         // view the block as an array of records
    rec[0] = randomRecord();
    rec[1] = randomRecord();
    BF_Block_SetDirty(block);
    CALL_OR_DIE(BF_UnpinBlock(block));
  }
  CALL_OR_DIE(BF_CloseFile(fd1));               // close the file and release its buffers
  CALL_OR_DIE(BF_Close());




  /* Part 2: reopen the file and read back the first two records of every block. */

  CALL_OR_DIE(BF_Init(LRU));
  CALL_OR_DIE(BF_OpenFile("block_example.db", &fd1));
  int blocks_num;
  CALL_OR_DIE(BF_GetBlockCounter(fd1, &blocks_num));

  for (int i = 0; i < blocks_num; ++i) {
    printf("Contents of Block %d\n\t",i);
    CALL_OR_DIE(BF_GetBlock(fd1, i, block));
    data = BF_Block_GetData(block);
    Record* rec= data;
    printRecord(rec[0]);
    printf("\t");
    printRecord(rec[1]);
    CALL_OR_DIE(BF_UnpinBlock(block));
  }

  BF_Block_Destroy(&block);
  CALL_OR_DIE(BF_CloseFile(fd1));
  CALL_OR_DIE(BF_Close());

}

