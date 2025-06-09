
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

#ifndef MEAN_TIME_NUM
#define MEAN_TIME_NUM (NUM_SLOTS)
#endif

long recorded_nano = 0;
double mean_waiting_time = 0.0;
unsigned long record = 0;
unsigned long round_number = 0;
long start_round = 0;
long end_round;
long min_waiting_time = (1L << 31) - 1;
long max_waiting_time = -1;
long now_nsec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}


int barrier_timer(void) {
    int ret;
    int last_thread;
    long start_time = now_nsec();

    while (era_counter != THREADS && control_counter == THREADS);

    ret = __sync_bool_compare_and_swap(&control_counter, THREADS, 0);
    if (ret) {
        era_counter = 0;
    }

    last_thread = __sync_fetch_and_add(&control_counter, 1);

    if (last_thread == THREADS - 1) {
        long end_time = now_nsec();
        long waiting_time = end_time - start_time;
        if (waiting_time < min_waiting_time) {
            min_waiting_time = waiting_time;
        }
        if (waiting_time > max_waiting_time) {
            max_waiting_time = waiting_time;
        }
        
        recorded_nano += waiting_time;
        record++;

        if (record >= MEAN_TIME_NUM) {
            end_round = end_time;
            mean_waiting_time = (double)recorded_nano / (double)record;
            printf("Barrier stats: {\n\tRound: %lu,\n\tmeasure number: %lu,\n\tmin_barrier_time: %ld ns,\n\taverage_barrier_time: %.3f ns,\n\tmax_barrier_time: %ld ns,\n\tcumulative_barrier_time: %.3f ms,\n\tround_time: %.3f s\n}\n",
                round_number, 
                record,
                min_waiting_time,
                mean_waiting_time,
                max_waiting_time,
                (double)recorded_nano / 1.0e6,
                (double)(end_round - start_round) / 1.0e9
            );
            start_round = end_time;
            // Reset stats
            record = 0;
            max_waiting_time = -1;
            min_waiting_time = (1L << 31) - 1;
            recorded_nano = 0;
            round_number++;
        }
    }

    while (control_counter != THREADS);
    __sync_fetch_and_add(&era_counter, 1);

    return ret;
}
#endif

