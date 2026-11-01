#ifndef HP_FILE_H
#define HP_FILE_H
#include <record.h>

/*
 * Heap file: an unordered, append-only file of fixed-size records stored on
 * top of the BF block layer.
 *
 * Layout
 *   block 0      header block, holds an HP_info
 *   block 1..n   data blocks, records packed from the start of the block and
 *                an HP_block_info trailer in the last bytes of the block.
 *                Data blocks form a singly linked chain via next_block_id.
 */

/* File-level metadata, persisted in block 0. */
typedef struct {
    int last_block_id;      /* block currently receiving inserts (0 = file is empty) */
    int number_of_blocks;   /* total number of blocks, header included */
    int first_block_rec;    /* first data block in the chain (-1 = none) */
} HP_info;

/* Per-block metadata, stored at the end of every data block. */
typedef struct {
    int record_count;            /* records currently stored in the block */
    int current_block_capacity;  /* free bytes left for records */
    int next_block_id;           /* next data block in the chain (-1 = last) */
} HP_block_info;


/* Creates and initialises an empty heap file called fileName.
 * Returns 0 on success, -1 on failure. */
int HP_CreateFile(char *fileName);

/* Opens the heap file fileName, loads its header from block 0 and returns it.
 * *file_desc receives the BF file descriptor. Returns NULL on failure. */
HP_info* HP_OpenFile(char *fileName, int *file_desc);

/* Writes the header back to block 0, closes the file and frees header_info.
 * Returns 0 on success, -1 on failure. */
int HP_CloseFile(int file_desc, HP_info* header_info);

/* Appends a record to the heap file.
 * Returns the id of the block the record was written to, or -1 on failure. */
int HP_InsertEntry(int file_desc, HP_info* header_info, Record record);

/* Prints every record whose id equals `id` (the heap file has no primary key,
 * so there may be several). Returns the number of blocks read, or -1 on failure. */
int HP_GetAllEntries(int file_desc, HP_info* header_info, int id);

#endif // HP_FILE_H
