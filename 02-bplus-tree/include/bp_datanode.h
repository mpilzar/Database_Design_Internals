#ifndef BP_DATANODE_H
#define BP_DATANODE_H
#include <record.h>
#include <bf.h>
#include <bp_file.h>

/*
 * Data node (leaf). Occupies one block and stores whole records, sorted by id.
 *
 * Capacity is derived from the block size (8 records per 512-byte block) and
 * can be overridden at compile time, e.g. -DBP_DATA_CAPACITY=3 reproduces the
 * small order-4 tree of the original assignment, which the test suite uses to
 * force deep trees and frequent splits/merges.
 */
#ifndef BP_DATA_CAPACITY
#define BP_DATA_CAPACITY ((int)((BF_BLOCK_SIZE - 3 * sizeof(int)) / sizeof(Record)))
#endif

/* Minimum fill of a non-root data node. A split of a full node produces two
 * nodes with at least this many records, and two nodes at (min, min - 1)
 * always fit into one, so borrow/merge never overflows. */
#define BP_DATA_MIN ((BP_DATA_CAPACITY + 1) / 2)

typedef struct BPLUS_DATA_NODE {
    int type;                                /* BP_DATA_NODE */
    int numberOfRecords;                     /* records currently stored */
    int nextNode;                            /* block id of the next leaf, -1 for the last */
    Record storedRecords[BP_DATA_CAPACITY];  /* sorted by id */
} BPLUS_DATA_NODE;

_Static_assert(sizeof(BPLUS_DATA_NODE) <= BF_BLOCK_SIZE, "data node must fit in one block");
_Static_assert(BP_DATA_CAPACITY >= 2, "data node must hold at least 2 records");

/* Formats a block as an empty data node. */
void datanode_init(BPLUS_DATA_NODE* node);

/* Position of the record with the given id, or -1 if absent (binary search). */
int datanode_find(const BPLUS_DATA_NODE* node, int id);

/* Inserts a record keeping the node sorted. The node must not be full.
 * Returns the position the record was placed at. */
int datanode_add(BPLUS_DATA_NODE* node, Record record);

/* Removes the record at position pos, shifting the rest left. */
void datanode_remove_at(BPLUS_DATA_NODE* node, int pos);

#endif
