#include <stdio.h>
#include "chunk.h"

/*
 * Note on pinning: HP_GetRecord / HP_UpdateRecord leave the block they touch
 * pinned in the buffer pool, so every access below is paired with HP_Unpin.
 * Unpinned blocks stay cached (LRU), so a cursor walking through a block hits
 * the buffer pool rather than the disk.
 */

CHUNK_Iterator CHUNK_CreateIterator(int fileDesc, int blocksInChunk) {
    CHUNK_Iterator iterator;
    iterator.file_desc = fileDesc;
    iterator.current = 1;                       // block 0 is the heap file header
    iterator.lastBlocksID = HP_GetIdOfLastBlock(fileDesc);
    iterator.blocksInChunk = blocksInChunk;
    return iterator;
}

int CHUNK_GetNext(CHUNK_Iterator *iterator, CHUNK *chunk) {
    if (iterator->current > iterator->lastBlocksID)
        return -1;

    chunk->file_desc = iterator->file_desc;
    chunk->from_BlockId = iterator->current;
    chunk->to_BlockId = iterator->current + iterator->blocksInChunk - 1;
    if (chunk->to_BlockId > iterator->lastBlocksID)
        chunk->to_BlockId = iterator->lastBlocksID;
    chunk->blocksInChunk = chunk->to_BlockId - chunk->from_BlockId + 1;

    // Count the records actually present: the file's last block may be only
    // partially filled.
    chunk->recordsInChunk = 0;
    for (int b = chunk->from_BlockId; b <= chunk->to_BlockId; b++)
        chunk->recordsInChunk += HP_GetRecordCounter(iterator->file_desc, b);

    iterator->current += iterator->blocksInChunk;
    return 0;
}

/* Maps a record index inside the chunk to (block, slot). Every block except
 * the file's last is full, so the mapping is a division. */
static void locate(CHUNK *chunk, int i, int *blockId, int *slot) {
    int perBlock = HP_GetMaxRecordsInBlock(chunk->file_desc);
    *blockId = chunk->from_BlockId + i / perBlock;
    *slot = i % perBlock;
}

int CHUNK_GetIthRecordInChunk(CHUNK *chunk, int i, Record *record) {
    if (i < 0 || i >= chunk->recordsInChunk)
        return -1;
    int blockId, slot;
    locate(chunk, i, &blockId, &slot);
    int rc = HP_GetRecord(chunk->file_desc, blockId, slot, record);
    HP_Unpin(chunk->file_desc, blockId);
    return rc == -1 ? -1 : 0;
}

int CHUNK_UpdateIthRecord(CHUNK *chunk, int i, Record record) {
    if (i < 0 || i >= chunk->recordsInChunk)
        return -1;
    int blockId, slot;
    locate(chunk, i, &blockId, &slot);
    int rc = HP_UpdateRecord(chunk->file_desc, blockId, slot, record);
    HP_Unpin(chunk->file_desc, blockId);
    return rc == -1 ? -1 : 0;
}

void CHUNK_Print(CHUNK chunk) {
    printf("Chunk: blocks %d-%d, %d records\n", chunk.from_BlockId, chunk.to_BlockId, chunk.recordsInChunk);
    for (int i = 0; i < chunk.recordsInChunk; i++) {
        Record record;
        if (CHUNK_GetIthRecordInChunk(&chunk, i, &record) == 0) {
            printf("  %4d: ", i);
            printRecord(record);
        }
    }
}

CHUNK_RecordIterator CHUNK_CreateRecordIterator(CHUNK *chunk) {
    CHUNK_RecordIterator iterator;
    iterator.chunk = *chunk;
    iterator.currentBlockId = chunk->from_BlockId;
    iterator.cursor = 0;
    return iterator;
}

int CHUNK_GetNextRecord(CHUNK_RecordIterator *iterator, Record *record) {
    int fd = iterator->chunk.file_desc;

    // Skip past the end of the current block (also handles empty blocks).
    while (iterator->currentBlockId <= iterator->chunk.to_BlockId &&
           iterator->cursor >= HP_GetRecordCounter(fd, iterator->currentBlockId)) {
        iterator->currentBlockId++;
        iterator->cursor = 0;
    }
    if (iterator->currentBlockId > iterator->chunk.to_BlockId)
        return -1;

    int blockId = iterator->currentBlockId;
    int rc = HP_GetRecord(fd, blockId, iterator->cursor, record);
    HP_Unpin(fd, blockId);
    if (rc == -1)
        return -1;

    iterator->cursor++;
    return 0;
}
