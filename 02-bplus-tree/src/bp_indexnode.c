#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include <bp_indexnode.h>


void indexnode_init(BPLUS_INDEX_NODE* node, int key, int leftChild, int rightChild) {
    memset(node, 0, sizeof(BPLUS_INDEX_NODE));
    node->type = BP_INDEX_NODE;
    node->numberOfKeys = 1;
    node->keys[0] = key;
    node->children[0] = leftChild;
    node->children[1] = rightChild;
}

int indexnode_child_slot(const BPLUS_INDEX_NODE* node, int id) {
    /* Number of keys <= id, i.e. the first i with id < keys[i]. Keys equal to
     * `id` route right, because a separator is the smallest id of its right
     * subtree at the moment it was created. */
    int lo = 0, hi = node->numberOfKeys;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (node->keys[mid] <= id) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

void indexnode_insert_at(BPLUS_INDEX_NODE* node, int slot, int key, int rightChild) {
    int n = node->numberOfKeys;
    memmove(&node->keys[slot + 1], &node->keys[slot], (n - slot) * sizeof(int));
    memmove(&node->children[slot + 2], &node->children[slot + 1], (n - slot) * sizeof(int));
    node->keys[slot] = key;
    node->children[slot + 1] = rightChild;
    node->numberOfKeys++;
}

void indexnode_remove_at(BPLUS_INDEX_NODE* node, int keyPos) {
    int n = node->numberOfKeys;
    memmove(&node->keys[keyPos], &node->keys[keyPos + 1], (n - keyPos - 1) * sizeof(int));
    memmove(&node->children[keyPos + 1], &node->children[keyPos + 2], (n - keyPos - 1) * sizeof(int));
    node->numberOfKeys--;
}
