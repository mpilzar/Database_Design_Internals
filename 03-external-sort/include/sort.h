#ifndef SORT_H
#define SORT_H

#include <stdbool.h>
#include "chunk.h"

/* True if rec1 sorts after rec2 (order: surname, then name). */
bool shouldSwap(Record* rec1, Record* rec2);

/* Sort phase: sorts the file in place in chunks of numBlocksInChunk blocks,
 * leaving a sequence of sorted runs. */
void sort_FileInChunks(int file_desc, int numBlocksInChunk);

/* Sorts the records of one chunk in place by surname, then name. */
void sort_Chunk(CHUNK* chunk);

#endif  // SORT_H
