#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include <bp_datanode.h>


void datanode_init(BPLUS_DATA_NODE* node) {
    memset(node, 0, sizeof(BPLUS_DATA_NODE));
    node->type = BP_DATA_NODE;
    node->numberOfRecords = 0;
    node->nextNode = -1;
}

/* First position whose id is >= `id`. */
static int lower_bound(const BPLUS_DATA_NODE* node, int id) {
    int lo = 0, hi = node->numberOfRecords;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (node->storedRecords[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int datanode_find(const BPLUS_DATA_NODE* node, int id) {
    int pos = lower_bound(node, id);
    if (pos < node->numberOfRecords && node->storedRecords[pos].id == id)
        return pos;
    return -1;
}

int datanode_add(BPLUS_DATA_NODE* node, Record record) {
    int pos = lower_bound(node, record.id);
    memmove(&node->storedRecords[pos + 1], &node->storedRecords[pos],
            (node->numberOfRecords - pos) * sizeof(Record));
    node->storedRecords[pos] = record;
    node->numberOfRecords++;
    return pos;
}

void datanode_remove_at(BPLUS_DATA_NODE* node, int pos) {
    memmove(&node->storedRecords[pos], &node->storedRecords[pos + 1],
            (node->numberOfRecords - pos - 1) * sizeof(Record));
    node->numberOfRecords--;
}
