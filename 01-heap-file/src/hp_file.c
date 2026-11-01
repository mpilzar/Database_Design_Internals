#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"
#include "record.h"

#define HP_ERROR -1

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      return HP_ERROR;        \
    }                         \
  }

/* The HP_block_info trailer lives in the last bytes of every data block. */
#define BLOCK_INFO(data) ((HP_block_info*)((data) + BF_BLOCK_SIZE - sizeof(HP_block_info)))

/* Number of record bytes available in an empty data block. */
#define EMPTY_BLOCK_CAPACITY ((int)(BF_BLOCK_SIZE - sizeof(HP_block_info)))


int HP_CreateFile(char *fileName) {
  int fd;
  BF_Block* block;

  CALL_BF(BF_CreateFile(fileName));
  CALL_BF(BF_OpenFile(fileName, &fd));
  BF_Block_Init(&block);
  CALL_BF(BF_AllocateBlock(fd, block));        // block 0: header

  HP_info hp_info;
  hp_info.last_block_id = 0;                   // no data blocks yet
  hp_info.number_of_blocks = 1;                // just the header
  hp_info.first_block_rec = -1;                // empty record chain

  memcpy(BF_Block_GetData(block), &hp_info, sizeof(HP_info));

  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);
  CALL_BF(BF_CloseFile(fd));

  return 0;
}

HP_info* HP_OpenFile(char *fileName, int *file_desc) {
  BF_Block* block;

  if (BF_OpenFile(fileName, file_desc) != BF_OK)
    return NULL;

  BF_Block_Init(&block);
  if (BF_GetBlock(*file_desc, 0, block) != BF_OK) {
    BF_Block_Destroy(&block);
    BF_CloseFile(*file_desc);
    return NULL;
  }

  // The header is copied into memory for the lifetime of the open file and
  // written back by HP_CloseFile.
  HP_info* hp_info = malloc(sizeof(HP_info));
  memcpy(hp_info, BF_Block_GetData(block), sizeof(HP_info));

  BF_UnpinBlock(block);
  BF_Block_Destroy(&block);

  return hp_info;
}

int HP_CloseFile(int file_desc, HP_info* hp_info) {
  BF_Block* block;

  // Persist the (possibly updated) header so the file can be reopened later.
  BF_Block_Init(&block);
  CALL_BF(BF_GetBlock(file_desc, 0, block));
  memcpy(BF_Block_GetData(block), hp_info, sizeof(HP_info));
  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  CALL_BF(BF_CloseFile(file_desc));
  free(hp_info);
  return 0;
}

/* Allocates a fresh data block, writes `record` as its first record and
 * returns the new block's id (or -1). The block is left unpinned. */
static int start_new_block(int file_desc, HP_info* hp_info, Record* record) {
  BF_Block* block;
  BF_Block_Init(&block);
  CALL_BF(BF_AllocateBlock(file_desc, block));

  char* data = BF_Block_GetData(block);
  memcpy(data, record, sizeof(Record));

  HP_block_info* info = BLOCK_INFO(data);
  info->record_count = 1;
  info->next_block_id = -1;
  info->current_block_capacity = EMPTY_BLOCK_CAPACITY - sizeof(Record);

  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  hp_info->number_of_blocks++;
  return hp_info->number_of_blocks - 1;        // blocks are appended at the end
}

int HP_InsertEntry(int file_desc, HP_info* hp_info, Record record) {

  // Empty file: the record starts the chain in block 1.
  if (hp_info->last_block_id == 0) {
    int new_id = start_new_block(file_desc, hp_info, &record);
    if (new_id == HP_ERROR) return HP_ERROR;
    hp_info->first_block_rec = new_id;
    hp_info->last_block_id = new_id;
    return new_id;
  }

  BF_Block* block;
  BF_Block_Init(&block);
  CALL_BF(BF_GetBlock(file_desc, hp_info->last_block_id, block));

  char* data = BF_Block_GetData(block);
  HP_block_info* info = BLOCK_INFO(data);

  if (info->current_block_capacity >= (int)sizeof(Record)) {
    // Room left in the last block: append in place.
    memcpy(data + info->record_count * sizeof(Record), &record, sizeof(Record));
    info->record_count++;
    info->current_block_capacity -= sizeof(Record);
    BF_Block_SetDirty(block);
    CALL_BF(BF_UnpinBlock(block));
    BF_Block_Destroy(&block);
    return hp_info->last_block_id;
  }

  // Last block is full: start a new one and link it into the chain.
  int new_id = start_new_block(file_desc, hp_info, &record);
  if (new_id == HP_ERROR) return HP_ERROR;

  info->next_block_id = new_id;
  BF_Block_SetDirty(block);
  CALL_BF(BF_UnpinBlock(block));
  BF_Block_Destroy(&block);

  hp_info->last_block_id = new_id;
  return new_id;
}

int HP_GetAllEntries(int file_desc, HP_info* hp_info, int value) {

  int blocks_read = 0;
  int block_id = hp_info->first_block_rec;
  BF_Block* block;
  BF_Block_Init(&block);

  // Full scan of the block chain: a heap file offers no faster access path.
  while (block_id != -1) {
    CALL_BF(BF_GetBlock(file_desc, block_id, block));
    blocks_read++;

    char* data = BF_Block_GetData(block);
    HP_block_info* info = BLOCK_INFO(data);

    for (int i = 0; i < info->record_count; i++) {
      Record* record = (Record*)(data + i * sizeof(Record));
      if (record->id == value)
        printf("Record ID: %d, Name: %s, Surname: %s, City: %s\n",
               record->id, record->name, record->surname, record->city);
    }

    block_id = info->next_block_id;
    CALL_BF(BF_UnpinBlock(block));
  }

  BF_Block_Destroy(&block);
  return blocks_read;
}
