#ifndef BF_H
#define BF_H

/*
 * BF — block-level buffer manager (provided as a precompiled library, lib/libbf.so).
 *
 * Files are sequences of fixed-size blocks. Every block a caller works with is
 * fetched into an in-memory buffer pool and "pinned" there until the caller
 * unpins it. Modified blocks must be marked dirty; the manager writes them
 * back to disk on eviction or on close. When the pool is full, an unpinned
 * block is evicted according to the chosen replacement policy (LRU or MRU).
 *
 * Every layer in this repository (heap file, B+ tree, external sort) is built
 * strictly on top of this interface: no component reads or writes the
 * underlying files directly.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define BF_BLOCK_SIZE 512      /* Size of a block in bytes */
#define BF_BUFFER_SIZE 100     /* Maximum number of blocks held in memory */
#define BF_MAX_OPEN_FILES 100  /* Maximum number of simultaneously open files */

typedef enum BF_ErrorCode {
  BF_OK,
  BF_OPEN_FILES_LIMIT_ERROR,     /* BF_MAX_OPEN_FILES files are already open */
  BF_INVALID_FILE_ERROR,         /* The file descriptor does not refer to an open file */
  BF_ACTIVE_ERROR,               /* The BF layer is already active and cannot be re-initialised */
  BF_FILE_ALREADY_EXISTS,        /* The file cannot be created because it already exists */
  BF_FULL_MEMORY_ERROR,          /* The buffer pool is full of pinned blocks */
  BF_INVALID_BLOCK_NUMBER_ERROR, /* The requested block does not exist in the file */
  BF_AVAILABLE_PIN_BLOCKS_ERROR, /* The file cannot be closed while it still has pinned blocks */
  BF_ERROR
} BF_ErrorCode;

typedef enum ReplacementAlgorithm {
  LRU,
  MRU
} ReplacementAlgorithm;

/* Opaque handle to a block in the buffer pool. */
typedef struct BF_Block BF_Block;

/* Allocates / frees the BF_Block handle itself (not the block's data). */
void BF_Block_Init(BF_Block **block);
void BF_Block_Destroy(BF_Block **block);

/* Marks the block as modified so that it is written back to disk. */
void BF_Block_SetDirty(BF_Block *block);

/* Returns a pointer to the block's BF_BLOCK_SIZE bytes of data in the pool.
 * The memory belongs to the buffer manager and must never be freed. */
char* BF_Block_GetData(const BF_Block *block);

/* Initialises the BF layer with the given replacement policy. */
BF_ErrorCode BF_Init(const ReplacementAlgorithm repl_alg);

/* Creates a new, empty block file. Fails if the file already exists. */
BF_ErrorCode BF_CreateFile(const char* filename);

/* Opens an existing block file and returns its descriptor in *file_desc. */
BF_ErrorCode BF_OpenFile(const char* filename, int *file_desc);

/* Closes an open block file. All of its blocks must be unpinned. */
BF_ErrorCode BF_CloseFile(const int file_desc);

/* Returns the number of blocks in the file in *blocks_num. */
BF_ErrorCode BF_GetBlockCounter(const int file_desc, int *blocks_num);

/* Appends a new block to the end of the file (its number is
 * BF_GetBlockCounter() - 1) and returns it pinned in *block. */
BF_ErrorCode BF_AllocateBlock(const int file_desc, BF_Block *block);

/* Fetches block number block_num of the file and returns it pinned. */
BF_ErrorCode BF_GetBlock(const int file_desc,
                         const int block_num,
                         BF_Block *block);

/* Unpins a block, allowing the manager to evict it (writing it back if dirty). */
BF_ErrorCode BF_UnpinBlock(BF_Block *block);

/* Prints a human-readable description of an error code to stderr. */
void BF_PrintError(BF_ErrorCode err);

/* Shuts down the BF layer, flushing every buffered block to disk. */
BF_ErrorCode BF_Close();

#ifdef __cplusplus
}
#endif
#endif // BF_H
