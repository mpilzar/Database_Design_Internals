/*
 * Heap file driver.
 *
 *   test1: insert N records, then look up 10 randomly sampled ids.
 *   test2: same, but the file is closed and the BF layer shut down between the
 *          inserts and the lookups, so every lookup is served from what was
 *          actually persisted to disk.
 *
 * Usage: ./hp_main [number_of_records]   (default 1000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bf.h"
#include "hp_file.h"

#define FILE_NAME "data.db"

void insertEntries(int file_desc, HP_info *hp_info, int entryNumber, Record sampledRecords[10]);
void printEntries(int file_desc, HP_info *hp_info, Record sampledRecords[10]);
void test1(int entryNumber);
void test2(int entryNumber);

int main(int argc, char *argv[]) {
  int entryNumber = argc > 1 ? atoi(argv[1]) : 1000;
  if (entryNumber < 10) entryNumber = 10;

  printf("=== test1: insert %d records and search (same session) ===\n", entryNumber);
  test1(entryNumber);

  printf("\n=== test2: insert %d records, close, reopen and search ===\n", entryNumber);
  test2(entryNumber);
  return 0;
}

void test1(int entryNumber) {
  remove(FILE_NAME);
  BF_Init(LRU);
  HP_CreateFile(FILE_NAME);
  int file_desc;
  HP_info *hp_info = HP_OpenFile(FILE_NAME, &file_desc);

  Record sampledRecords[10];
  insertEntries(file_desc, hp_info, entryNumber, sampledRecords);
  printEntries(file_desc, hp_info, sampledRecords);

  HP_CloseFile(file_desc, hp_info);
  BF_Close();
}

void test2(int entryNumber) {
  remove(FILE_NAME);
  BF_Init(LRU);
  HP_CreateFile(FILE_NAME);
  int file_desc;
  HP_info *hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  Record sampledRecords[10];
  insertEntries(file_desc, hp_info, entryNumber, sampledRecords);
  HP_CloseFile(file_desc, hp_info);
  BF_Close();

  BF_Init(LRU);
  hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  printEntries(file_desc, hp_info, sampledRecords);
  HP_CloseFile(file_desc, hp_info);
  BF_Close();
}

/* Inserts records until 10 of them have been sampled (each record is sampled
 * with probability 10/entryNumber), so roughly entryNumber records in total. */
void insertEntries(int file_desc, HP_info *hp_info, int entryNumber, Record sampledRecords[10]) {
  int counter = 0;
  while (counter < 10) {
    double random_value = (double)rand() / RAND_MAX;
    Record record = randomRecord();
    HP_InsertEntry(file_desc, hp_info, record);
    if (random_value <= (double)10 / entryNumber)
      sampledRecords[counter++] = record;
  }
}

void printEntries(int file_desc, HP_info *hp_info, Record sampledRecords[10]) {
  for (int i = 0; i < 10; i++) {
    printf("Searching: *%d*\n", sampledRecords[i].id);
    int blocks = HP_GetAllEntries(file_desc, hp_info, sampledRecords[i].id);
    printf("  (%d blocks scanned)\n", blocks);
  }
}
