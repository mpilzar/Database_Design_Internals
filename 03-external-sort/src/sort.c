#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"
#include "record.h"
#include "sort.h"
#include "merge.h"
#include "chunk.h"

/* Sort order: by surname, then by name. */
bool shouldSwap(Record *rec1, Record *rec2) {
    int cmpSurname = strcmp(rec1->surname, rec2->surname);
    if (cmpSurname != 0) return cmpSurname > 0;
    return strcmp(rec1->name, rec2->name) > 0;
}

static int compare_records(const void *a, const void *b) {
    Record *ra = (Record *)a, *rb = (Record *)b;
    if (shouldSwap(ra, rb)) return 1;
    if (shouldSwap(rb, ra)) return -1;
    return 0;
}

/* Sort phase for one chunk. A chunk is sized to fit in the buffer pool, so
 * its records are read once, sorted in memory, and written back in place. */
void sort_Chunk(CHUNK *chunk) {
    int n = chunk->recordsInChunk;
    if (n < 2) return;

    Record *records = malloc(n * sizeof(Record));
    for (int i = 0; i < n; i++)
        CHUNK_GetIthRecordInChunk(chunk, i, &records[i]);

    qsort(records, n, sizeof(Record), compare_records);

    for (int i = 0; i < n; i++)
        CHUNK_UpdateIthRecord(chunk, i, records[i]);
    free(records);
}

/* Turns the file into a sequence of sorted runs of numBlocksInChunk blocks. */
void sort_FileInChunks(int file_desc, int numBlocksInChunk) {
    CHUNK_Iterator iterator = CHUNK_CreateIterator(file_desc, numBlocksInChunk);
    CHUNK chunk;

    while (CHUNK_GetNext(&iterator, &chunk) == 0)
        sort_Chunk(&chunk);
}
