
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <queue.h>



long control_counter __attribute__((aligned(64))) = THREADS;
long era_counter __attribute__((aligned(64))) = THREADS;

int barrier(void){

    int ret;

    while(era_counter != THREADS && control_counter == THREADS);

    ret = __sync_bool_compare_and_swap(&control_counter,THREADS,0);
    if( ret ) era_counter = 0;

    __sync_fetch_and_add(&control_counter,1);
    while(control_counter != THREADS);
    __sync_fetch_and_add(&era_counter,1);

    return ret;
}

#ifdef BARRIER_TIMER
#include <time.h>
#include <stdbool.h>

#ifndef MEAN_TIME_NUM
#define MEAN_TIME_NUM (NUM_SLOTS)
#endif

#define BATCH1_SIZE (MEAN_TIME_NUM)
#define BATCH2_SIZE (MEAN_TIME_NUM / 2)
#define BATCH3_SIZE (MEAN_TIME_NUM / 8)

struct BatchStats {
    long sum;
    unsigned long count;
    long min;
    long max;
};

struct BatchSnapshot {
    long min;
    long max;
    double mean;
    unsigned long count;
};

// Batch data
struct BatchStats batch1 = {0, 0, (1L << 31) - 1, -1};
struct BatchStats batch2 = {0, 0, (1L << 31) - 1, -1};
struct BatchStats batch3 = {0, 0, (1L << 31) - 1, -1};

// Latest completed snapshots for batch2 and batch3
struct BatchSnapshot snapshot2 = {0, 0, 0.0, 0};
struct BatchSnapshot snapshot3 = {0, 0, 0.0, 0};

// Global statistics
long global_sum = 0;
unsigned long global_count = 0;
long global_min = (1L << 31) - 1;
long global_max = -1;

// Control flags and round timing
bool first_skipped = false;
unsigned long round_number = 0;
long start_round = 0;

long now_nsec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

void update_batch(struct BatchStats* batch, long delta) {
    batch->sum += delta;
    batch->count++;
    if (delta < batch->min) batch->min = delta;
    if (delta > batch->max) batch->max = delta;
}

void reset_batch(struct BatchStats* batch) {
    batch->sum = 0;
    batch->count = 0;
    batch->min = (1L << 31) - 1;
    batch->max = -1;
}

int barrier_timer(void) {
    int ret;
    long start_time = now_nsec();

    while (era_counter != THREADS && control_counter == THREADS);
    ret = __sync_bool_compare_and_swap(&control_counter, THREADS, 0);
    if (ret) era_counter = 0;

    int last_thread = __sync_fetch_and_add(&control_counter, 1);
    if (last_thread == THREADS - 1) {
        long end_time = now_nsec();
        long delta = end_time - start_time;

        if (!first_skipped) {
            first_skipped = true;
            start_round = end_time;
        } else {
            // Update batches
            update_batch(&batch1, delta);
            update_batch(&batch2, delta);
            update_batch(&batch3, delta);

            // Update global
            global_sum += delta;
            global_count++;
            if (delta < global_min) global_min = delta;
            if (delta > global_max) global_max = delta;

            // Handle snapshot & reset of Batch2
            if (batch2.count >= BATCH2_SIZE) {
                snapshot2.count = batch2.count;
                snapshot2.mean = (double)batch2.sum / batch2.count;
                snapshot2.min = batch2.min;
                snapshot2.max = batch2.max;
                reset_batch(&batch2);
            }

            // Handle snapshot & reset of Batch3
            if (batch3.count >= BATCH3_SIZE) {
                snapshot3.count = batch3.count;
                snapshot3.mean = (double)batch3.sum / batch3.count;
                snapshot3.min = batch3.min;
                snapshot3.max = batch3.max;
                reset_batch(&batch3);
            }

            // Handle Batch1 print and reset
            if (batch1.count >= BATCH1_SIZE) {
                double mean1 = (double)batch1.sum / batch1.count;
                double global_mean = (double)global_sum / global_count;
            

                printf("Barrier measures: {"
                       "\"Round\": %lu, "
                       "\"Batch1 (N=%d)\": { \"count\": %lu, \"min\": %ld, \"mean\": %.3f, \"max\": %ld, \"max-min\": %ld }, "
                       "\"Batch2 (N=%d)\": { \"count\": %lu, \"min\": %ld, \"mean\": %.3f, \"max\": %ld, \"max-min\": %ld }, "
                       "\"Batch3 (N=%d)\": { \"count\": %lu, \"min\": %ld, \"mean\": %.3f, \"max\": %ld, \"max-min\": %ld }, "
                       "\"Global\": { \"count\": %lu, \"min\": %ld, \"mean\": %.3f, \"max\": %ld },"
                       "\"Batch cumulative\": %ld ms, "
                       "\"Total barrier's time\": %ld"
                       "}\n",
                       round_number,
                       BATCH1_SIZE, batch1.count, batch1.min, mean1, batch1.max, batch1.max - batch1.min,
                       BATCH2_SIZE, snapshot2.count, snapshot2.min, snapshot2.mean, snapshot2.max, snapshot2.max - snapshot2.min,
                       BATCH3_SIZE, snapshot3.count, snapshot3.min, snapshot3.mean, snapshot3.max, snapshot3.max - snapshot3.min,
                       global_count, global_min, global_mean, global_max,
                       batch1.sum,
                       (end_time - start_round));

                reset_batch(&batch1);
                reset_batch(&batch2);
                reset_batch(&batch3);
                snapshot2 = (struct BatchSnapshot){0, 0, 0.0, 0};
                snapshot3 = (struct BatchSnapshot){0, 0, 0.0, 0};

                round_number++;
                start_round = end_time;
            }
        }
    }

    while (control_counter != THREADS);
    __sync_fetch_and_add(&era_counter, 1);
    return ret;
}
#endif
