#ifndef CHUNK_H
#define CHUNK_H

#include <stdio.h>
#include <stdlib.h>
#include "hp_file.h"

/*
 * A CHUNK is a run of consecutive blocks of a heap file, described only by its
 * block range: it is a view onto the file, not a copy. The sort phase sorts
 * each chunk in place; the merge phase treats every chunk as a sorted run.
 */

/* A contiguous range of blocks [from_BlockId, to_BlockId] of a heap file. */
typedef struct {
    int file_desc;
    int from_BlockId;
    int to_BlockId;
    int recordsInChunk;
    int blocksInChunk;
} CHUNK;

/* Iterates over a heap file in consecutive chunks of blocksInChunk blocks
 * (the last chunk may be shorter). Block 0 is the heap file's header. */
typedef struct {
    int file_desc;
    int current;
    int lastBlocksID;
    int blocksInChunk;
} CHUNK_Iterator;

/* Creates an iterator over the chunks of the file, starting at block 1. */
CHUNK_Iterator CHUNK_CreateIterator(int fileDesc, int blocksInChunk);

/* Fills *chunk with the next chunk. Returns 0, or -1 when there are no more. */
int CHUNK_GetNext(CHUNK_Iterator *iterator, CHUNK* chunk);

/* Reads the i-th record of the chunk (0-based). Returns 0, or -1 if out of range. */
int CHUNK_GetIthRecordInChunk(CHUNK* chunk, int i, Record* record);

/* Overwrites the i-th record of the chunk. Returns 0, or -1 if out of range. */
int CHUNK_UpdateIthRecord(CHUNK* chunk, int i, Record record);

/* Prints the records of a chunk. */
void CHUNK_Print(CHUNK chunk);


/* Sequential cursor over the records of one chunk. */
typedef struct CHUNK_RecordIterator {
    CHUNK chunk;
    int currentBlockId;
    int cursor;
} CHUNK_RecordIterator;

/* Creates a record iterator positioned at the chunk's first record. */
CHUNK_RecordIterator CHUNK_CreateRecordIterator(CHUNK *chunk);

/* Reads the next record. Returns 0, or -1 when the chunk is exhausted. */
int CHUNK_GetNextRecord(CHUNK_RecordIterator *iterator, Record* record);

#endif  // CHUNK_H
