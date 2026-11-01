#ifndef MERGE_H
#define MERGE_H
#include "sort.h"

/* One merge pass: merges every bWay consecutive sorted runs of chunkSize
 * blocks from the input file into one sorted run, appended to the output
 * file. Uses a CHUNK_Iterator to find the runs and one CHUNK_RecordIterator
 * per run as its read cursor. */
void merge(int input_FileDesc, int chunkSize, int bWay, int output_FileDesc);

#endif  // MERGE_H
