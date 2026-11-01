#ifndef BP_FILE_H
#define BP_FILE_H
#include <record.h>

/*
 * Disk-resident B+ tree index over Record.id, built on the BF block layer.
 *
 * Every node occupies exactly one block and nodes refer to each other by
 * block number, so the whole structure lives in the file and survives being
 * closed and reopened.
 *
 *   block 0        file header (BPLUS_INFO)
 *   data nodes     leaves; hold the records themselves, sorted by id,
 *                  and are chained left-to-right through nextNode
 *   index nodes    internal nodes; hold separator keys and child block ids
 *   free blocks    blocks released by merges, kept on a free list and
 *                  recycled by later splits
 */

#define BP_MAGIC 0x42504C53            /* "BPLS": identifies a B+ tree file */

/* Node type tag stored in the first int of every node block. */
#define BP_DATA_NODE  1
#define BP_INDEX_NODE 2
#define BP_FREE_NODE  3

/* File header, persisted in block 0 and cached in memory while the file is open. */
typedef struct {
    int magic;             /* BP_MAGIC */
    int root;              /* block id of the root node, -1 when the tree is empty */
    int height;            /* 0 = empty, 1 = root is a data node, >1 = root is an index node */
    int numberOfBlocks;    /* blocks in the file, header and free blocks included */
    int numberOfRecords;   /* records currently stored */
    int freeList;          /* first block of the free list, -1 when empty */
} BPLUS_INFO;


/* Creates an empty B+ tree file and writes its header to block 0.
 * Returns 0 on success, -1 on failure. */
int BP_CreateFile(char *fileName);

/* Opens a B+ tree file and returns its header (heap-allocated).
 * *file_desc receives the BF file descriptor. Returns NULL on failure. */
BPLUS_INFO* BP_OpenFile(char *fileName, int *file_desc);

/* Writes the header back to block 0, closes the file and frees `info`.
 * Returns 0 on success, -1 on failure. */
int BP_CloseFile(int file_desc, BPLUS_INFO* info);

/* Inserts a record, keyed by record.id. Splits overflowing nodes on the way
 * back up, growing the tree by one level when the root splits.
 * Returns the block id of the data node that received the record,
 * or -1 if a record with the same id already exists (or on error). */
int BP_InsertEntry(int file_desc, BPLUS_INFO* bplus_info, Record record);

/* Looks up the record with the given id.
 * On success copies it into **result and returns 0; if *result is NULL it is
 * first pointed at an internal buffer that stays valid until the next call.
 * If there is no such record, sets *result to NULL and returns -1. */
int BP_GetEntry(int file_desc, BPLUS_INFO* header_info, int id, Record** result);

/* Deletes the record with the given id. Underflowing nodes are rebalanced by
 * borrowing from a sibling or merging with it; merges cascade upwards and the
 * tree loses a level when the root is left with a single child. Released
 * blocks go onto the free list.
 * Returns 0 on success, -1 if there is no such record (or on error). */
int BP_DeleteEntry(int file_desc, BPLUS_INFO* bplus_info, int id);

/* Prints the tree level by level (keys only), for debugging and demos. */
void BP_PrintTree(int file_desc, BPLUS_INFO* bplus_info);

#endif
