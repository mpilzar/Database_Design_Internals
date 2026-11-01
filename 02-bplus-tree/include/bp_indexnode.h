#ifndef BP_INDEX_NODE_H
#define BP_INDEX_NODE_H
#include <record.h>
#include <bf.h>
#include <bp_file.h>

/*
 * Index node (internal). Occupies one block and stores n separator keys and
 * n + 1 child block ids. For every i, all ids under children[i] are < keys[i]
 * and all ids under children[i + 1] are >= keys[i].
 *
 * Capacity is derived from the block size (62 keys / fan-out 63 per 512-byte
 * block) and can be overridden at compile time with -DBP_INDEX_CAPACITY=n.
 */
#ifndef BP_INDEX_CAPACITY
#define BP_INDEX_CAPACITY ((int)((BF_BLOCK_SIZE - 3 * sizeof(int)) / (2 * sizeof(int))))
#endif

/* Minimum number of keys in a non-root index node. */
#define BP_INDEX_MIN (BP_INDEX_CAPACITY / 2)

typedef struct BPLUS_INDEX_NODE {
    int type;                               /* BP_INDEX_NODE */
    int numberOfKeys;                       /* keys currently stored */
    int keys[BP_INDEX_CAPACITY];            /* separator keys, ascending */
    int children[BP_INDEX_CAPACITY + 1];    /* child block ids */
} BPLUS_INDEX_NODE;

/* An index node with room for one extra key and child. A full node that
 * receives a new key is first materialised as a BIG node, then cut in two
 * around its middle key, which moves up to the parent. */
typedef struct BIG_BPLUS_INDEX_NODE {
    int numberOfKeys;
    int keys[BP_INDEX_CAPACITY + 1];
    int children[BP_INDEX_CAPACITY + 2];
} BIG_BPLUS_INDEX_NODE;

_Static_assert(sizeof(BPLUS_INDEX_NODE) <= BF_BLOCK_SIZE, "index node must fit in one block");
_Static_assert(BP_INDEX_CAPACITY >= 2, "index node must hold at least 2 keys");

/* Formats a block as an index node with a single key and two children. */
void indexnode_init(BPLUS_INDEX_NODE* node, int key, int leftChild, int rightChild);

/* Index of the child subtree that may contain `id` (binary search). */
int indexnode_child_slot(const BPLUS_INDEX_NODE* node, int id);

/* Inserts `key` at keys[slot] and `rightChild` at children[slot + 1],
 * shifting what follows. The node must not be full. */
void indexnode_insert_at(BPLUS_INDEX_NODE* node, int slot, int key, int rightChild);

/* Removes keys[keyPos] and children[keyPos + 1], shifting what follows. */
void indexnode_remove_at(BPLUS_INDEX_NODE* node, int keyPos);

#endif
