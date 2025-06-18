#ifndef WORKLOAD_DISTRIBUTION
#define WORKLOAD_DISTRIBUTION
#endif
#include <queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setup.h>
#include "memory.h"
#include <sys/signal.h>
#include <assert.h>
#include <stdbool.h>

slot queue[OBJECTS][NUM_SLOTS];
lock_buffer locks[OBJECTS][NUM_SLOTS];
long long total_events[OBJECTS] = {[0 ... OBJECTS - 1] 0};
long long total_times[OBJECTS] = {[0 ... OBJECTS - 1] 0};
double volatile current_min_limit = 0.0;
double volatile current_max_limit = NUM_SLOTS * LOOKAHEAD;
int volatile current_index = 0;

long pending_events __attribute__((aligned(64))) = 0;
volatile long object_identifiers __attribute__((aligned(64))) = 0;
long object_identifiers_vector[MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
int end = 0;

__thread int my_index = 0;
__thread fallback_slot fallback_queue; //WE INITIALIZE VIA EMPTY ZERO MEMORY = { .head = NULL , .tail = NULL };

__thread unsigned me;
__thread volatile unsigned target = -1;

//we use '_' here just to discriminate from the
//corresponding non TLS global variables
__thread int * _c;
__thread int * _min;
__thread int * _max;
//these are used for NUMA aware workload distribution
__thread int myNUMAnode;
__thread int myNUMAindex;
__thread int stealNUMAindex;
__thread int TOT_NUMA_NODES;


// [LONGEST]
typedef struct LongestId{
        long long ID __attribute__((aligned(64)));
        long long weight __attribute__((aligned(64)));
}LongestId_t;

// this have to be per_thread variable because each thread is pinned to a specific cpu. to avoid inconsistent tick took

LongestId_t pool[OBJECTS] __attribute__((aligned(64)));    
LongestId_t temp[OBJECTS] __attribute__((aligned(64))); // buffer temporaneo per il merge

__thread long long _start_time = 0;
__thread long long _end_time = 0;


void whoami(unsigned my_id){
    AUDIT printf("just audit whoami: %u\n",my_id);
    me = my_id;
    _c = getcounter();
    _min = getmin();
    _max = getmax();
    myNUMAindex = myNUMAnode = get_NUMAnode();
    stealNUMAindex = 0;
    TOT_NUMA_NODES = get_totNUMAnodes();
}

int queue_init(void){

    int i;
    int j;
    queue_elem * head;
    queue_elem * tail;

    for(j = 0; j < OBJECTS; j++){
        for (i = 0; i < NUM_SLOTS; i++) {
            head = &queue[j][i].head;
            tail = &queue[j][i].tail;
            head->next = tail;//setup initial double linked list
            tail->prev = head;
            head->timestamp = -1;//setup initial timestamp value
            tail->timestamp = -1;
            queue[j][i].num_events = 0; // setup intial number of events
			queue[j][i].event_mean_time = 0;

            pthread_spin_init(&locks[j][i].lock,PTHREAD_PROCESS_PRIVATE);
        }
    }
    for (i = 0; i < OBJECTS; i++){
        pool[i].ID = i;
        pool[i].weight = 0;
    }

    return 1;
}

void merge(LongestId_t arr[], int left, int mid, int right) {
    int i = left;
    int j = mid + 1;
    int k = left;

    while (i <= mid && j <= right) {
        if (arr[i].weight >= arr[j].weight)
            temp[k++] = arr[i++];
        else
            temp[k++] = arr[j++];
    }

    while (i <= mid)
        temp[k++] = arr[i++];
    while (j <= right)
        temp[k++] = arr[j++];

    for (i = left; i <= right; i++)
        arr[i] = temp[i];
}

void merge_sort(LongestId_t arr[], int left, int right) {
    if (left >= right)
        return;

    int mid = (left + right) / 2;
    merge_sort(arr, left, mid);
    merge_sort(arr, mid + 1, right);
    merge(arr, left, mid, right);
}

void update_timing(void){

    int i;
    int j;
    unsigned prev_index = current_index;
    current_min_limit += LOOKAHEAD;
    current_max_limit += LOOKAHEAD;
    current_index = (current_index + 1)%NUM_SLOTS;

    AUDIT{
        printf("updating timing - current min is %e - current max is %e - current index is %d\n",current_min_limit,current_max_limit, current_index);
        fflush(stdout);
    }
    object_identifiers = 0;
	for(i = 0; i < MAX_NUMA_NODES ; i++) object_identifiers_vector[i] = 0;
    //printf("before segmentation fault %d\n", my_index);
    merge_sort(pool, 0, OBJECTS - 1);
    if(!pending_events) end = 1;
}

int queue_insert(queue_elem * elem){

    queue_elem * current;
    queue_elem * tail;
    int index;
    int dest;

    AUDIT printf("just audit who I am: %u\n",me);

    if(elem->timestamp < current_min_limit){
        printf("illegal queue insert - timestamp is %e - min limit is %e\n",elem->timestamp,current_min_limit);
        return -1;

    }

    if (elem->timestamp >= current_max_limit){
        //here we make a tail insert - there will be no next
        elem->next = NULL;
        if (fallback_queue.head == NULL){
            elem->prev = NULL;
            fallback_queue.head = elem;
            fallback_queue.tail = elem;
        }
        else{
            elem->prev = fallback_queue.tail;
            fallback_queue.tail->next = elem;
            fallback_queue.tail = elem;
        }
        __sync_fetch_and_add(&pending_events,1);
        return 0;
    }

    index = (int)((elem->timestamp) / (double)SLOT_LEN);
    index = index % NUM_SLOTS;

    AUDIT{
        printf("inserting event with timestamp %e in slot %d\n",elem->timestamp, index);
        fflush(stdout);
    }

    dest = elem->destination;

    current = &queue[dest][index].head;
    tail = &queue[dest][index].tail;

    AUDIT{
        printf("queue_insert for an element with timestamp %e\n",elem->timestamp);
        fflush(stdout);
    }

    pthread_spin_lock(&locks[dest][index].lock);

    while(current->timestamp <= elem->timestamp && current->next != tail){
        current = current->next;
    }

    elem->next = current->next;//link to the subsequent element
    current->next = elem;

    elem->next->prev = elem;//relink the previoous elements
    elem->prev = current;
    __sync_fetch_and_add(&queue[dest][index].num_events, 1); // inc the number of the events in the slot
    __sync_fetch_and_add(&pending_events,1);//there is one more element in the queue

    pthread_spin_unlock(&locks[dest][index].lock);

    return 0;
}

void fallback_check(void){
    queue_elem * temp = fallback_queue.head;//the fallback_queue is __thread hence
    //we already run isolated on this queue
    queue_elem * aux;
    queue_elem * current;
    queue_elem * tail;
    int index;
    int dest;

    while(temp){
        if (temp->timestamp >= current_max_limit){temp = temp->next;}

        else{
            aux = temp->next;
            if (fallback_queue.head == temp) fallback_queue.head = temp->next;
            if (fallback_queue.tail == temp) fallback_queue.tail = temp->prev;
            if (temp->next) temp->next->prev = temp->prev;
            if (temp->prev) temp->prev->next = temp->next;
            index = (int)((temp->timestamp) / (double)SLOT_LEN);
            index = index % NUM_SLOTS;
            dest = temp->destination;
            current = &queue[dest][index].head;//here current is not the object but
            //the target queue head
            tail = &queue[dest][index].tail;

            AUDIT {
                printf("queue_insert (from fallback) for an element with timestamp %e\n",temp->timestamp);
                fflush(stdout);
            }

            pthread_spin_lock(&locks[dest][index].lock);

            while(current->timestamp <= temp->timestamp && current->next != tail){
                current = current->next;
            }
            temp->next = current->next;//link to the subsequent element
            current->next = temp;
            temp->next->prev = temp;//relink the previous elements
            temp->prev = current;
            __sync_fetch_and_add(&queue[dest][index].num_events, 1); // inc the number of the events in the slot from the fallback queue
            pthread_spin_unlock(&locks[dest][index].lock);


            temp = aux;
        }
    }

}

long long processed = 0;
queue_elem * queue_extract(){

	int index;
    long long mean;
	queue_elem * head;
	queue_elem * tail;
	queue_elem * elem;
    long long pool_index;
	AUDIT printf("thread %d - extraction with target %d\n",me,target);

start:
#ifndef NUMA_BALANCING
	if (target == -1) {
        pool_index = __sync_fetch_and_add(&object_identifiers,1);
        target =  pool[pool_index].ID;
        took_tick(&_start_time);
    }
#else
	if (target == -1){
retry:
		target = pool[__sync_fetch_and_add(&object_identifiers_vector[myNUMAindex],1)].ID; 
		if (target >= _c[myNUMAindex]){
			if(stealNUMAindex < TOT_NUMA_NODES){
				stealNUMAindex++;
				myNUMAindex = (myNUMAindex+1)%TOT_NUMA_NODES;
				goto retry;
			}
			else target = OBJECTS;
		}
		else{
			target += _min[myNUMAindex];
		}
        

        took_tick(&_start_time);
	}

#endif

redo:
	if (end) return NULL;
	if (target < OBJECTS) {
		index = my_index;
		head = &queue[target][index].head;
		tail = &queue[target][index].tail;

		if( head->next == tail) { //the current slot is empty - try with another target 
#ifndef NUMA_BALANCING
            pool_index = __sync_fetch_and_add(&object_identifiers,1);
            if (pool_index >= OBJECTS){
                target = pool_index;
                goto redo;
            }
			target =  pool[pool_index].ID;
#else
            pool_index = __sync_fetch_and_add(&object_identifiers_vector[myNUMAindex],1);
            if (pool_index >= OBJECTS){
                target = pool_index;
                goto redo;
            }
			target = pool[pool_index].ID; 
			if (target >= _c[myNUMAindex]){
				goto retry;
				//the below stuff (if/else) is useless and can be removed
				if(stealNUMAindex < TOT_NUMA_NODES){
					stealNUMAindex++;
					myNUMAindex = (myNUMAindex+1)%TOT_NUMA_NODES;
					index = -1;
					goto start;
				}
				else target = OBJECTS;
			}
			else{
				target += _min[myNUMAindex];
			}
#endif
            took_tick(&_start_time);
			goto redo;
		}
	}
	else{
		AUDIT{
			printf("found empty slot with index %d\n",index);
			fflush(stdout);
		}
#ifndef BARRIER_TIMER
	if( barrier())
#else
    if (barrier_timer())
#endif
		{
			update_timing();//this call updates the queue layout and releases the objects taken by threads in the last epoch
		}

#ifdef NUMA_UBIQUITOUS
		if (to_restore != -1) mm_restore();
#endif
		barrier();
		my_index = current_index;
		target = -1;
		//reset stuff for NUMA aware workload distribution
		myNUMAindex = myNUMAnode;
		stealNUMAindex = 0;
		fallback_check();
		goto start;
	}

	pthread_spin_lock(&locks[target][index].lock);
// [LONGEST] mean updating
	if( head->next == tail) { //the current slot is empty  
		pthread_spin_unlock(&locks[target][index].lock);
        took_tick(&_end_time);
        // calculating the mean for the LP
        total_times[target] += (_end_time - _start_time);
        total_events[target] += queue[target][my_index].num_events;
        mean = total_events[target] / total_times[target];
        queue[target][(my_index + 1) % NUM_SLOTS].event_mean_time = mean;  
        queue[target][my_index].num_events = 0;
        pool[target].weight = mean;
		if (end){
		       	return NULL;
		}
		else goto redo;
	}

	//regular extraction from a non-empty slot



#ifdef NUMA_UBIQUITOUS
	num_events[count_index]++;


	if ((target == to_restore)){
		AUDIT printf("found the same object %d to process\n",target);
		goto process;
	}
	if (to_restore != -1){
		AUDIT printf("restoring mm for object %d\n",to_restore);
		mm_restore();
		 
	}

#ifdef TLB_TEST
	if ((++taken_objects)%THRESHOLD == 0){
		AUDIT printf("setting mm for object %d - just TLB test\n",target);
		mm_set(target);	
	}
	goto process;
#endif
	if (maps[target].primary_node != myNUMAnode){
		AUDIT printf("setting mm for object %d\n",target);
		mm_set(target);	
	}
	AUDIT printf("simply processing local object %d\n",target);
process:
#endif

	elem = head->next;

	head->next = elem->next;
	elem->next->prev = head;

	__sync_fetch_and_add(&pending_events,-1);
	pthread_spin_unlock(&locks[target][index].lock);

	AUDIT printf("just returning from queue_extract\n");
	return elem;
	
}
