/*
 * External merge sort driver.
 *
 * Each test fills a heap file with random records, runs the sort phase
 * (sorted runs of chunkSize blocks) and then b-way merge passes until a
 * single sorted run remains, writing every pass to a new file
 * (<name>1.db, <name>2.db, ...). It then reports how many sorted runs each
 * file contains: the count should drop by a factor of b per pass, down to 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "merge.h"


int createAndPopulateHeapFile(char *filename, int totalRecords);
void sortPhase(int file_desc, int chunkSize);
void mergePhases(char *filename,int inputFileDesc, int chunkSize, int bWay, int *fileCounter);
int nextOutputFile(char *filename,int *fileCounter);
void test(char* filename,int totalRecords, int chunkSize, int bWay);

int main()
{
	BF_Init(LRU);
	test("./test1.db",4608,1,2);
    test("./test2.db",11520,5,4);
    BF_Close();
    /* Expected output
    File ./test1.db has 512 blocks
    After the sort phase, file ./test1.db has 512 chunk(s)
    After the merge phase 1, the output file ./test1.db1.db has 256 chunk(s)
    After the merge phase 2, the output file ./test1.db2.db has 128 chunk(s)
    After the merge phase 3, the output file ./test1.db3.db has 64 chunk(s)
    After the merge phase 4, the output file ./test1.db4.db has 32 chunk(s)
    After the merge phase 5, the output file ./test1.db5.db has 16 chunk(s)
    After the merge phase 6, the output file ./test1.db6.db has 8 chunk(s)
    After the merge phase 7, the output file ./test1.db7.db has 4 chunk(s)
    After the merge phase 8, the output file ./test1.db8.db has 2 chunk(s)
    After the merge phase 9, the output file ./test1.db9.db has 1 chunk(s)
    File ./test2.db has 1280 blocks
    After the sort phase, file ./test2.db has 256 chunk(s)
    After the merge phase 1, the output file ./test2.db1.db has 64 chunk(s)
    After the merge phase 2, the output file ./test2.db2.db has 16 chunk(s)
    After the merge phase 3, the output file ./test2.db3.db has 4 chunk(s)
    After the merge phase 4, the output file ./test2.db4.db has 1 chunk(s)*/
}


int createAndPopulateHeapFile(char *filename, int totalRecords)
{
	HP_CreateFile(filename);

	int file_desc;
	HP_OpenFile(filename, &file_desc);

	Record record;
	srand(12569874);
	for (int id = 0; id < totalRecords; ++id)
	{
		record = randomRecord();
		HP_InsertEntry(file_desc, record);
	}
	return file_desc;
}

/*Performs the sorting phase of external merge sort algorithm on a file specified by 'file_desc', using chunks of size 'chunkSize'*/
void sortPhase(int file_desc, int chunkSize)
{
	sort_FileInChunks(file_desc, chunkSize);
}

/* Performs the merge phase of the external merge sort algorithm  using chunks of size 'chunkSize' and 'bWay' merging. The merge phase may be performed in more than one cycles.*/
void mergePhases(char *filename, int inputFileDesc, int chunkSize, int bWay, int *fileCounter)
{
	int outputFileDesc = -1;

	while (chunkSize < HP_GetIdOfLastBlock(inputFileDesc))
	{
		outputFileDesc = nextOutputFile(filename,fileCounter);
		merge(inputFileDesc, chunkSize, bWay, outputFileDesc);
		HP_CloseFile(inputFileDesc);
		chunkSize *= bWay;
		inputFileDesc = outputFileDesc;
	}

	if (outputFileDesc != -1)
		HP_CloseFile(outputFileDesc);
}

/* Creates the next output file of the sequence <filename>1.db, <filename>2.db, ... and returns its descriptor. */
int nextOutputFile(char *filename,int *fileCounter)
{
	char mergedFile[50];
	sprintf(mergedFile, "%s%d.db", filename, (*fileCounter)++);
	int file_desc;
	HP_CreateFile(mergedFile);
	HP_OpenFile(mergedFile, &file_desc);
	return file_desc;
}

int getNumberOfChunks(char *filename);


/**
 * @brief Executes and validates an external merge sort on a heap file.
 *
 * This function creates a heap file, populates it with random records, 
 * sorts it in chunks, and performs multi-way merge phases until the file 
 * is fully sorted. It reports the number of chunks after each phase.
 *
 * @param fileName      Name of the heap file to process.
 * @param totalRecords  Number of random records to populate in the file.
 * @param chunkSize     Size (in blocks) of chunks for sorting and merging.
 * @param bWay          Number of chunks to merge simultaneously in each phase.
 *
 * @output
 * Prints the number of chunks after the sorting phase and each merge phase.
 *
 * Example:
 * test("./test1.db", 2700, 1, 2); // Processes file "./test1.db".
 */


// This function checks if two records are in the correct order based on name and surname
bool areRecordsInOrder(Record recordA, Record recordB)
{
    if (strcmp(recordA.surname, recordB.surname) > 0)
    {
        return false;
    }
    return true;
}

// This function checks if all records in a block are sorted
bool isBlockSorted(int fileDescriptor, int blockId)
{
    int totalRecords = HP_GetRecordCounter(fileDescriptor, blockId);

    // If the block has one or no records, it is sorted by definition
    if (totalRecords <= 1)
    {
        return true;
    }

    // Compare each record with the next to ensure the block is sorted
    Record currentRecord;
    HP_GetRecord(fileDescriptor, blockId, 0, &currentRecord);
    Record nextRecord;
    for (int i = 1; i < totalRecords; i++)
    {
        HP_GetRecord(fileDescriptor, blockId, i, &nextRecord);
        if (!areRecordsInOrder(currentRecord, nextRecord))
        {
            return false;
        }
        currentRecord = nextRecord;
    }
    return true;
}

// This function calculates the number of sorted chunks in a heap file
int getNumberOfChunks(char *fileName)
{
    int fileDescriptor;

    // Open the heap file
    HP_OpenFile(fileName, &fileDescriptor);

    int chunkCount = 0;
    int previousBlockId = 1;

    // Get the ID of the last block in the file
    int lastBlockId = HP_GetIdOfLastBlock(fileDescriptor);

    // If the file has no blocks or the first block is not sorted, return an error
    if (lastBlockId < 1 || !isBlockSorted(fileDescriptor, previousBlockId))
    {
        HP_Unpin(fileDescriptor, previousBlockId);
        return -1;
    }

    // Iterate through all blocks to identify sorted chunks
    for (int currentBlockId = 2; currentBlockId <= lastBlockId; currentBlockId++)
    {
        if (!isBlockSorted(fileDescriptor, currentBlockId))
        {
            HP_Unpin(fileDescriptor, previousBlockId);
            HP_Unpin(fileDescriptor, currentBlockId);
            return -1;
        }

        Record lastRecordOfPreviousBlock;
        Record firstRecordOfCurrentBlock;

        // Compare the last record of the previous block with the first record of the current block
        HP_GetRecord(fileDescriptor, previousBlockId, HP_GetRecordCounter(fileDescriptor, previousBlockId) - 1, &lastRecordOfPreviousBlock);
        HP_GetRecord(fileDescriptor, currentBlockId, 0, &firstRecordOfCurrentBlock);

        if (!areRecordsInOrder(lastRecordOfPreviousBlock, firstRecordOfCurrentBlock))
        {
            chunkCount++;
        }
        HP_Unpin(fileDescriptor, previousBlockId);
        previousBlockId = currentBlockId;
    }

    // Account for the last chunk
    HP_Unpin(fileDescriptor, previousBlockId);
    chunkCount++;

    // Close the file
    HP_CloseFile(fileDescriptor);
    return chunkCount;
}


void test(char* fileName, int totalRecords, int chunkSize,int bWay){
    // Remove the files of any previous run
    char staleFile[64];
    remove(fileName);
    for (int i = 1; i < 64; i++) {
        sprintf(staleFile, "%s%d.db", fileName, i);
        remove(staleFile);
    }
    clock_t start = clock();

	// Create and populate the heap file with initial data
    int heapFileDescriptor = createAndPopulateHeapFile(fileName,totalRecords);
    printf("File %s has %d blocks\n",fileName,HP_GetIdOfLastBlock(heapFileDescriptor));


    // Perform the sorting phase on the heap file
    sortPhase(heapFileDescriptor, chunkSize);

    
    // Initialize iterator for merge phases
    int mergeIterator = 1;
    
    // Perform the merge phases to further organize data in the heap file
    mergePhases(fileName,heapFileDescriptor, chunkSize, bWay, &mergeIterator);

	// Count the number of chunks in the file after the sorting phase
    int chunkCount = getNumberOfChunks(fileName);
    printf("After the sort phase, file %s has %d chunk(s)\n", fileName, chunkCount);
    // Check the state of merged files
    char mergedFileName[50];
    for (int i = 1; i < mergeIterator; i++)
    {
        sprintf(mergedFileName, "%s%d.db", fileName, i);
        int mergedChunkCount = getNumberOfChunks(mergedFileName);
        printf("After the merge phase %d, the output file %s has %d chunk(s)\n",i,mergedFileName, mergedChunkCount);
    }
    printf("(%d records sorted in %.2f s)\n\n", totalRecords, (double)(clock() - start) / CLOCKS_PER_SEC);
}

