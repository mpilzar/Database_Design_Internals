#include <merge.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/*
 * One merge pass of external merge sort.
 *
 * The input file is a sequence of sorted runs (chunks) of chunkSize blocks.
 * Runs are taken bWay at a time; each group is merged into a single sorted
 * run of up to bWay * chunkSize blocks, appended to the output file. After
 * the pass the output holds ceil(runs / bWay) runs, so ceil(log_b(runs))
 * passes turn the sort phase's output into one sorted file.
 *
 * Only one cursor per input run and the output's tail block are active at a
 * time, so a pass needs about bWay + 1 buffer blocks regardless of file size,
 * and every block is read once and written once per pass.
 */
void merge(int input_FileDesc, int chunkSize, int bWay, int output_FileDesc) {
    CHUNK_Iterator chunks = CHUNK_CreateIterator(input_FileDesc, chunkSize);
    CHUNK_RecordIterator *runs = malloc(bWay * sizeof(CHUNK_RecordIterator));
    Record *heads = malloc(bWay * sizeof(Record));   // current record of each run
    bool *live = malloc(bWay * sizeof(bool));         // run still has records
    CHUNK chunk;

    for (;;) {
        // Open the next group of up to bWay runs.
        int k = 0;
        while (k < bWay && CHUNK_GetNext(&chunks, &chunk) == 0) {
            runs[k] = CHUNK_CreateRecordIterator(&chunk);
            live[k] = CHUNK_GetNextRecord(&runs[k], &heads[k]) == 0;
            k++;
        }
        if (k == 0)
            break;

        // Repeatedly emit the smallest head and advance that run. Ties go to
        // the earlier run, which keeps the merge stable.
        for (;;) {
            int best = -1;
            for (int i = 0; i < k; i++)
                if (live[i] && (best == -1 || shouldSwap(&heads[best], &heads[i])))
                    best = i;
            if (best == -1)
                break;

            HP_InsertEntry(output_FileDesc, heads[best]);
            live[best] = CHUNK_GetNextRecord(&runs[best], &heads[best]) == 0;
        }
    }

    free(runs);
    free(heads);
    free(live);
}
