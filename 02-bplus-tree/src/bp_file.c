#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "bp_file.h"
#include "record.h"
#include <bp_datanode.h>
#include <bp_indexnode.h>

#define bplus_ERROR -1
#define BP_MAX_HEIGHT 64

#define CALL_BF(call)         \
  {                           \
    BF_ErrorCode code = call; \
    if (code != BF_OK) {      \
      BF_PrintError(code);    \
      return bplus_ERROR;     \
    }                         \
  }

/* Like CALL_BF, but jumps to the function's `fail` label so that BF_Block
 * handles are released on the way out. */
#define TRY(call)                \
  do {                           \
    BF_ErrorCode code_ = (call); \
    if (code_ != BF_OK) {        \
      BF_PrintError(code_);      \
      goto fail;                 \
    }                            \
  } while (0)

#define DATA(block)  ((BPLUS_DATA_NODE*)BF_Block_GetData(block))
#define INDEX(block) ((BPLUS_INDEX_NODE*)BF_Block_GetData(block))

/* A block released by a merge. Free blocks form a singly linked list whose
 * head is BPLUS_INFO.freeList. */
typedef struct {
  int type;   /* BP_FREE_NODE */
  int next;   /* next free block, -1 for the last */
} BPLUS_FREE_NODE;

/* Root-to-leaf path recorded while descending: the index nodes visited and
 * the child slot taken at each. Splits and merges walk this path back up,
 * which is why nodes do not need to store a pointer to their parent (keeping
 * parent pointers on disk would mean rewriting every moved child's block on
 * each split). */
typedef struct {
  int depth;
  int block[BP_MAX_HEIGHT];
  int slot[BP_MAX_HEIGHT];
} Path;


/* ------------------------------------------------------------------------ */
/* Block management                                                         */
/* ------------------------------------------------------------------------ */

static void release(BF_Block* block, int dirty) {
  if (dirty) BF_Block_SetDirty(block);
  BF_ErrorCode code = BF_UnpinBlock(block);
  if (code != BF_OK) BF_PrintError(code);
}

static void swap_blocks(BF_Block** a, BF_Block** b) {
  BF_Block* tmp = *a; *a = *b; *b = tmp;
}

/* Returns the id of a block for a new node, pinned into `block`. A block is
 * recycled from the free list when one is available, otherwise one is
 * appended to the file. */
static int node_alloc(int file_desc, BPLUS_INFO* info, BF_Block* block) {
  if (info->freeList != -1) {
    int id = info->freeList;
    CALL_BF(BF_GetBlock(file_desc, id, block));
    info->freeList = ((BPLUS_FREE_NODE*)BF_Block_GetData(block))->next;
    return id;
  }
  CALL_BF(BF_AllocateBlock(file_desc, block));
  int blocks;
  CALL_BF(BF_GetBlockCounter(file_desc, &blocks));
  info->numberOfBlocks = blocks;
  return blocks - 1;
}

/* Pushes the (pinned) block `id` onto the free list and unpins it. */
static void node_free(BPLUS_INFO* info, BF_Block* block, int id) {
  BPLUS_FREE_NODE* node = (BPLUS_FREE_NODE*)BF_Block_GetData(block);
  memset(node, 0, BF_BLOCK_SIZE);
  node->type = BP_FREE_NODE;
  node->next = info->freeList;
  info->freeList = id;
  release(block, 1);
}

/* Walks from the root to the data node that may hold `id`, recording the
 * path. Reads exactly height - 1 index blocks. Returns the data node's
 * block id, or -1 on error. */
static int descend(int file_desc, const BPLUS_INFO* info, int id, Path* path) {
  BF_Block* block;
  BF_Block_Init(&block);

  int current = info->root;
  path->depth = 0;
  for (int level = info->height; level > 1; level--) {
    if (BF_GetBlock(file_desc, current, block) != BF_OK) {
      BF_Block_Destroy(&block);
      return -1;
    }
    BPLUS_INDEX_NODE* node = INDEX(block);
    int slot = indexnode_child_slot(node, id);
    path->block[path->depth] = current;
    path->slot[path->depth] = slot;
    path->depth++;
    current = node->children[slot];
    release(block, 0);
  }

  BF_Block_Destroy(&block);
  return current;
}


/* ------------------------------------------------------------------------ */
/* File-level operations                                                    */
/* ------------------------------------------------------------------------ */

int BP_CreateFile(char *fileName) {
  int fd;
  BF_Block* block;

  CALL_BF(BF_CreateFile(fileName));
  CALL_BF(BF_OpenFile(fileName, &fd));
  BF_Block_Init(&block);
  CALL_BF(BF_AllocateBlock(fd, block));

  BPLUS_INFO info;
  info.magic = BP_MAGIC;
  info.root = -1;
  info.height = 0;
  info.numberOfBlocks = 1;
  info.numberOfRecords = 0;
  info.freeList = -1;
  memcpy(BF_Block_GetData(block), &info, sizeof(BPLUS_INFO));

  release(block, 1);
  BF_Block_Destroy(&block);
  CALL_BF(BF_CloseFile(fd));
  return 0;
}

BPLUS_INFO* BP_OpenFile(char *fileName, int *file_desc) {
  BF_Block* block;

  if (BF_OpenFile(fileName, file_desc) != BF_OK) {
    fprintf(stderr, "BP_OpenFile: could not open %s\n", fileName);
    return NULL;
  }

  BF_Block_Init(&block);
  if (BF_GetBlock(*file_desc, 0, block) != BF_OK) {
    BF_Block_Destroy(&block);
    BF_CloseFile(*file_desc);
    fprintf(stderr, "BP_OpenFile: could not read the header block of %s\n", fileName);
    return NULL;
  }

  BPLUS_INFO* info = malloc(sizeof(BPLUS_INFO));
  memcpy(info, BF_Block_GetData(block), sizeof(BPLUS_INFO));
  release(block, 0);
  BF_Block_Destroy(&block);

  if (info->magic != BP_MAGIC) {
    fprintf(stderr, "BP_OpenFile: %s is not a B+ tree file\n", fileName);
    free(info);
    BF_CloseFile(*file_desc);
    return NULL;
  }
  return info;
}

int BP_CloseFile(int file_desc, BPLUS_INFO* info) {
  BF_Block* block;

  /* The header is cached in memory while the file is open; persist it. */
  BF_Block_Init(&block);
  CALL_BF(BF_GetBlock(file_desc, 0, block));
  memcpy(BF_Block_GetData(block), info, sizeof(BPLUS_INFO));
  release(block, 1);
  BF_Block_Destroy(&block);

  CALL_BF(BF_CloseFile(file_desc));
  free(info);
  return 0;
}


/* ------------------------------------------------------------------------ */
/* Search                                                                   */
/* ------------------------------------------------------------------------ */

int BP_GetEntry(int file_desc, BPLUS_INFO* info, int id, Record** result) {
  static Record lookup_buffer;

  if (info->root == -1) {
    *result = NULL;
    return -1;
  }

  Path path;
  int leafId = descend(file_desc, info, id, &path);
  if (leafId < 0) { *result = NULL; return bplus_ERROR; }

  BF_Block* block;
  BF_Block_Init(&block);
  if (BF_GetBlock(file_desc, leafId, block) != BF_OK) {
    BF_Block_Destroy(&block);
    *result = NULL;
    return bplus_ERROR;
  }

  BPLUS_DATA_NODE* leaf = DATA(block);
  int pos = datanode_find(leaf, id);
  int rc;
  if (pos >= 0) {
    if (*result == NULL) *result = &lookup_buffer;
    **result = leaf->storedRecords[pos];
    rc = 0;
  } else {
    *result = NULL;
    rc = -1;
  }

  release(block, 0);
  BF_Block_Destroy(&block);
  return rc;
}


/* ------------------------------------------------------------------------ */
/* Insertion                                                                */
/* ------------------------------------------------------------------------ */

int BP_InsertEntry(int file_desc, BPLUS_INFO* info, Record record) {
  BF_Block *block, *other;
  BF_Block_Init(&block);
  BF_Block_Init(&other);
  int result = bplus_ERROR;

  /* Empty tree: the first data node becomes the root. */
  if (info->root == -1) {
    int id = node_alloc(file_desc, info, block);
    if (id < 0) goto fail;
    datanode_init(DATA(block));
    datanode_add(DATA(block), record);
    release(block, 1);
    info->root = id;
    info->height = 1;
    info->numberOfRecords++;
    result = id;
    goto done;
  }

  Path path;
  int leafId = descend(file_desc, info, record.id, &path);
  if (leafId < 0) goto fail;

  TRY(BF_GetBlock(file_desc, leafId, block));
  BPLUS_DATA_NODE* leaf = DATA(block);

  /* Ids are unique keys. */
  if (datanode_find(leaf, record.id) >= 0) {
    release(block, 0);
    goto done;
  }

  /* Room in the data node: insert in place. */
  if (leaf->numberOfRecords < BP_DATA_CAPACITY) {
    datanode_add(leaf, record);
    release(block, 1);
    info->numberOfRecords++;
    result = leafId;
    goto done;
  }

  /* Data node overflow. Lay out all CAPACITY + 1 records in order, keep the
   * lower half in the existing block and move the upper half into a new
   * block, so only one block is allocated per split. */
  Record all[BP_DATA_CAPACITY + 1];
  int pos = BP_DATA_CAPACITY;
  memcpy(all, leaf->storedRecords, BP_DATA_CAPACITY * sizeof(Record));
  while (pos > 0 && all[pos - 1].id > record.id) {
    all[pos] = all[pos - 1];
    pos--;
  }
  all[pos] = record;

  int leftCount = (BP_DATA_CAPACITY + 2) / 2;              /* ceil((CAP + 1) / 2) */
  int rightCount = BP_DATA_CAPACITY + 1 - leftCount;

  int newLeafId = node_alloc(file_desc, info, other);
  if (newLeafId < 0) goto fail;
  BPLUS_DATA_NODE* right = DATA(other);
  datanode_init(right);
  memcpy(right->storedRecords, all + leftCount, rightCount * sizeof(Record));
  right->numberOfRecords = rightCount;
  right->nextNode = leaf->nextNode;                        /* keep the leaf chain intact */

  memset(leaf->storedRecords, 0, sizeof(leaf->storedRecords));
  memcpy(leaf->storedRecords, all, leftCount * sizeof(Record));
  leaf->numberOfRecords = leftCount;
  leaf->nextNode = newLeafId;

  result = (pos < leftCount) ? leafId : newLeafId;
  int sepKey = right->storedRecords[0].id;                 /* smallest id of the new node */
  int rightChild = newLeafId;
  release(block, 1);
  release(other, 1);
  info->numberOfRecords++;

  /* Propagate (sepKey, rightChild) up the recorded path until some index
   * node has room for it. */
  int level;
  for (level = path.depth - 1; level >= 0; level--) {
    int nodeId = path.block[level];
    int slot = path.slot[level];

    TRY(BF_GetBlock(file_desc, nodeId, block));
    BPLUS_INDEX_NODE* node = INDEX(block);

    if (node->numberOfKeys < BP_INDEX_CAPACITY) {
      indexnode_insert_at(node, slot, sepKey, rightChild);
      release(block, 1);
      break;
    }

    /* Index node overflow: materialise the one-larger BIG node with the new
     * key in place, then cut it around its middle key. The lower part stays
     * in this block, the upper part goes to a new block and the middle key
     * moves up a level. */
    BIG_BPLUS_INDEX_NODE big;
    big.numberOfKeys = BP_INDEX_CAPACITY + 1;
    memcpy(big.keys, node->keys, slot * sizeof(int));
    big.keys[slot] = sepKey;
    memcpy(big.keys + slot + 1, node->keys + slot, (BP_INDEX_CAPACITY - slot) * sizeof(int));
    memcpy(big.children, node->children, (slot + 1) * sizeof(int));
    big.children[slot + 1] = rightChild;
    memcpy(big.children + slot + 2, node->children + slot + 1, (BP_INDEX_CAPACITY - slot) * sizeof(int));

    int leftKeys = (BP_INDEX_CAPACITY + 1) / 2;
    int rightKeys = big.numberOfKeys - leftKeys - 1;

    int newIndexId = node_alloc(file_desc, info, other);
    if (newIndexId < 0) goto fail;
    BPLUS_INDEX_NODE* sibling = INDEX(other);
    memset(sibling, 0, sizeof(BPLUS_INDEX_NODE));
    sibling->type = BP_INDEX_NODE;
    sibling->numberOfKeys = rightKeys;
    memcpy(sibling->keys, big.keys + leftKeys + 1, rightKeys * sizeof(int));
    memcpy(sibling->children, big.children + leftKeys + 1, (rightKeys + 1) * sizeof(int));

    memset(node->keys, 0, sizeof(node->keys));
    memset(node->children, 0, sizeof(node->children));
    node->numberOfKeys = leftKeys;
    memcpy(node->keys, big.keys, leftKeys * sizeof(int));
    memcpy(node->children, big.children, (leftKeys + 1) * sizeof(int));

    sepKey = big.keys[leftKeys];
    rightChild = newIndexId;
    release(block, 1);
    release(other, 1);
  }

  /* The split reached the root: grow the tree by one level. */
  if (level < 0) {
    int rootId = node_alloc(file_desc, info, block);
    if (rootId < 0) goto fail;
    indexnode_init(INDEX(block), sepKey, info->root, rightChild);
    release(block, 1);
    info->root = rootId;
    info->height++;
  }

done:
  BF_Block_Destroy(&block);
  BF_Block_Destroy(&other);
  return result;

fail:
  BF_Block_Destroy(&block);
  BF_Block_Destroy(&other);
  return bplus_ERROR;
}


/* ------------------------------------------------------------------------ */
/* Deletion                                                                 */
/* ------------------------------------------------------------------------ */

int BP_DeleteEntry(int file_desc, BPLUS_INFO* info, int id) {
  if (info->root == -1) return -1;

  BF_Block *nodeBlk, *parentBlk, *leftBlk, *rightBlk;
  BF_Block_Init(&nodeBlk);
  BF_Block_Init(&parentBlk);
  BF_Block_Init(&leftBlk);
  BF_Block_Init(&rightBlk);
  int result = -1;

  Path path;
  int nodeId = descend(file_desc, info, id, &path);
  if (nodeId < 0) goto fail;

  TRY(BF_GetBlock(file_desc, nodeId, nodeBlk));
  BPLUS_DATA_NODE* leaf = DATA(nodeBlk);
  int pos = datanode_find(leaf, id);
  if (pos < 0) {
    release(nodeBlk, 0);
    goto done;                                             /* no such record */
  }
  datanode_remove_at(leaf, pos);
  info->numberOfRecords--;
  result = 0;

  /* A root data node may hold any number of records; when it empties, so
   * does the tree. */
  if (path.depth == 0) {
    if (leaf->numberOfRecords == 0) {
      node_free(info, nodeBlk, nodeId);
      info->root = -1;
      info->height = 0;
    } else {
      release(nodeBlk, 1);
    }
    goto done;
  }

  if (leaf->numberOfRecords >= BP_DATA_MIN) {
    release(nodeBlk, 1);
    goto done;
  }

  /* ---- Data node underflow: borrow from a sibling, or merge with one. ---- */
  int level = path.depth - 1;
  int parentId = path.block[level];
  int slot = path.slot[level];
  TRY(BF_GetBlock(file_desc, parentId, parentBlk));
  BPLUS_INDEX_NODE* parent = INDEX(parentBlk);

  int leftId = slot > 0 ? parent->children[slot - 1] : -1;
  int rightId = slot < parent->numberOfKeys ? parent->children[slot + 1] : -1;
  BPLUS_DATA_NODE *leftLeaf = NULL, *rightLeaf = NULL;

  if (leftId != -1) {
    TRY(BF_GetBlock(file_desc, leftId, leftBlk));
    leftLeaf = DATA(leftBlk);
    if (leftLeaf->numberOfRecords > BP_DATA_MIN) {
      /* Borrow the left sibling's largest record; it becomes our smallest,
       * so the separator between us moves down to it. */
      datanode_add(leaf, leftLeaf->storedRecords[leftLeaf->numberOfRecords - 1]);
      leftLeaf->numberOfRecords--;
      parent->keys[slot - 1] = leaf->storedRecords[0].id;
      release(leftBlk, 1);
      release(nodeBlk, 1);
      release(parentBlk, 1);
      goto done;
    }
  }
  if (rightId != -1) {
    TRY(BF_GetBlock(file_desc, rightId, rightBlk));
    rightLeaf = DATA(rightBlk);
    if (rightLeaf->numberOfRecords > BP_DATA_MIN) {
      /* Borrow the right sibling's smallest record; the separator moves up to
       * the right sibling's new smallest id. */
      leaf->storedRecords[leaf->numberOfRecords++] = rightLeaf->storedRecords[0];
      datanode_remove_at(rightLeaf, 0);
      parent->keys[slot] = rightLeaf->storedRecords[0].id;
      if (leftLeaf) release(leftBlk, 0);
      release(rightBlk, 1);
      release(nodeBlk, 1);
      release(parentBlk, 1);
      goto done;
    }
  }

  /* Neither sibling can spare a record, so merge. Prefer folding this node
   * into its left sibling; otherwise absorb the right sibling. */
  if (leftLeaf) {
    memcpy(leftLeaf->storedRecords + leftLeaf->numberOfRecords, leaf->storedRecords,
           leaf->numberOfRecords * sizeof(Record));
    leftLeaf->numberOfRecords += leaf->numberOfRecords;
    leftLeaf->nextNode = leaf->nextNode;
    release(leftBlk, 1);
    if (rightLeaf) release(rightBlk, 0);
    node_free(info, nodeBlk, nodeId);
    indexnode_remove_at(parent, slot - 1);
  } else {
    memcpy(leaf->storedRecords + leaf->numberOfRecords, rightLeaf->storedRecords,
           rightLeaf->numberOfRecords * sizeof(Record));
    leaf->numberOfRecords += rightLeaf->numberOfRecords;
    leaf->nextNode = rightLeaf->nextNode;
    release(nodeBlk, 1);
    node_free(info, rightBlk, rightId);
    indexnode_remove_at(parent, slot);
  }

  /* ---- The parent lost a key. Walk up while index nodes underflow. ---- */
  swap_blocks(&nodeBlk, &parentBlk);                       /* the parent is now the node */
  nodeId = parentId;

  for (;;) {
    BPLUS_INDEX_NODE* node = INDEX(nodeBlk);

    if (level == 0) {
      /* A root index node needs only one child; with none left to separate,
       * its single child becomes the new root and the tree shrinks. */
      if (node->numberOfKeys == 0) {
        info->root = node->children[0];
        info->height--;
        node_free(info, nodeBlk, nodeId);
      } else {
        release(nodeBlk, 1);
      }
      break;
    }

    if (node->numberOfKeys >= BP_INDEX_MIN) {
      release(nodeBlk, 1);
      break;
    }

    level--;
    parentId = path.block[level];
    slot = path.slot[level];
    TRY(BF_GetBlock(file_desc, parentId, parentBlk));
    parent = INDEX(parentBlk);

    leftId = slot > 0 ? parent->children[slot - 1] : -1;
    rightId = slot < parent->numberOfKeys ? parent->children[slot + 1] : -1;
    BPLUS_INDEX_NODE *left = NULL, *right = NULL;
    int n = node->numberOfKeys;

    if (leftId != -1) {
      TRY(BF_GetBlock(file_desc, leftId, leftBlk));
      left = INDEX(leftBlk);
      if (left->numberOfKeys > BP_INDEX_MIN) {
        /* Rotate right: the separator comes down as our first key and the
         * left sibling's last child and key move across / up. */
        memmove(node->keys + 1, node->keys, n * sizeof(int));
        memmove(node->children + 1, node->children, (n + 1) * sizeof(int));
        node->keys[0] = parent->keys[slot - 1];
        node->children[0] = left->children[left->numberOfKeys];
        node->numberOfKeys++;
        parent->keys[slot - 1] = left->keys[left->numberOfKeys - 1];
        left->numberOfKeys--;
        release(leftBlk, 1);
        release(nodeBlk, 1);
        release(parentBlk, 1);
        break;
      }
    }
    if (rightId != -1) {
      TRY(BF_GetBlock(file_desc, rightId, rightBlk));
      right = INDEX(rightBlk);
      if (right->numberOfKeys > BP_INDEX_MIN) {
        /* Rotate left: the mirror image of the above. */
        int rn = right->numberOfKeys;
        node->keys[n] = parent->keys[slot];
        node->children[n + 1] = right->children[0];
        node->numberOfKeys++;
        parent->keys[slot] = right->keys[0];
        memmove(right->keys, right->keys + 1, (rn - 1) * sizeof(int));
        memmove(right->children, right->children + 1, rn * sizeof(int));
        right->numberOfKeys--;
        if (left) release(leftBlk, 0);
        release(rightBlk, 1);
        release(nodeBlk, 1);
        release(parentBlk, 1);
        break;
      }
    }

    /* Merge two index nodes, pulling their separator down between them. */
    if (left) {
      int ln = left->numberOfKeys;
      left->keys[ln] = parent->keys[slot - 1];
      memcpy(left->keys + ln + 1, node->keys, n * sizeof(int));
      memcpy(left->children + ln + 1, node->children, (n + 1) * sizeof(int));
      left->numberOfKeys = ln + 1 + n;
      release(leftBlk, 1);
      if (right) release(rightBlk, 0);
      node_free(info, nodeBlk, nodeId);
      indexnode_remove_at(parent, slot - 1);
    } else {
      int rn = right->numberOfKeys;
      node->keys[n] = parent->keys[slot];
      memcpy(node->keys + n + 1, right->keys, rn * sizeof(int));
      memcpy(node->children + n + 1, right->children, (rn + 1) * sizeof(int));
      node->numberOfKeys = n + 1 + rn;
      release(nodeBlk, 1);
      node_free(info, rightBlk, rightId);
      indexnode_remove_at(parent, slot);
    }

    swap_blocks(&nodeBlk, &parentBlk);
    nodeId = parentId;
  }

done:
  BF_Block_Destroy(&nodeBlk);
  BF_Block_Destroy(&parentBlk);
  BF_Block_Destroy(&leftBlk);
  BF_Block_Destroy(&rightBlk);
  return result;

fail:
  BF_Block_Destroy(&nodeBlk);
  BF_Block_Destroy(&parentBlk);
  BF_Block_Destroy(&leftBlk);
  BF_Block_Destroy(&rightBlk);
  return bplus_ERROR;
}


/* ------------------------------------------------------------------------ */
/* Debug output                                                             */
/* ------------------------------------------------------------------------ */

void BP_PrintTree(int file_desc, BPLUS_INFO* info) {
  printf("B+ tree: height %d, %d records, %d blocks\n",
         info->height, info->numberOfRecords, info->numberOfBlocks);
  if (info->root == -1) return;

  BF_Block* block;
  BF_Block_Init(&block);

  /* Level-order walk; each level's block ids are collected into `next`. */
  int count = 1;
  int* level = malloc(sizeof(int));
  level[0] = info->root;

  for (int depth = info->height; depth >= 1; depth--) {
    int nextCount = 0;
    int* next = malloc(sizeof(int) * (size_t)count * (BP_INDEX_CAPACITY + 1));
    printf("  L%d:", info->height - depth);
    for (int i = 0; i < count; i++) {
      if (BF_GetBlock(file_desc, level[i], block) != BF_OK) break;
      if (depth > 1) {
        BPLUS_INDEX_NODE* node = INDEX(block);
        printf(" [");
        for (int k = 0; k < node->numberOfKeys; k++)
          printf(k ? " %d" : "%d", node->keys[k]);
        printf("]");
        for (int c = 0; c <= node->numberOfKeys; c++)
          next[nextCount++] = node->children[c];
      } else {
        BPLUS_DATA_NODE* leaf = DATA(block);
        printf(" (");
        for (int r = 0; r < leaf->numberOfRecords; r++)
          printf(r ? " %d" : "%d", leaf->storedRecords[r].id);
        printf(")");
      }
      release(block, 0);
    }
    printf("\n");
    free(level);
    level = next;
    count = nextCount;
  }
  free(level);
  BF_Block_Destroy(&block);
}
