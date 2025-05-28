
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


#define MEAN_TIME_NUM (NUM_SLOTS)

long recorded_nano = 0;
double mean_waiting_time = 0.0;
unsigned long record = 0;
unsigned long timer_count = 0;

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
        
        recorded_nano += waiting_time;
        record++;

        if (record >= MEAN_TIME_NUM) {
            mean_waiting_time = (double)recorded_nano / (double)record;
            printf("Timer round %lu: mean waiting time=%.3f cumulative_time=%lld ns\n", timer_count + 1, 
                mean_waiting_time, recorded_nano);

            // Reset stats
            record = 0;
            recorded_nano = 0;
            timer_count++;
        }
    }

    while (control_counter != THREADS);
    __sync_fetch_and_add(&era_counter, 1);

    return ret;
}
#endif

