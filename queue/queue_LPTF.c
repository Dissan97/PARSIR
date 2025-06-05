#include <queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setup.h>
#include "memory.h"
#include <sys/signal.h>
#include <assert.h>
#ifdef WORKLOAD_DISTRIBUTION
#include <stdbool.h>
#endif

slot queue[OBJECTS][NUM_SLOTS];
lock_buffer locks[OBJECTS][NUM_SLOTS];

double volatile current_min_limit = 0.0;
double volatile current_max_limit = NUM_SLOTS * LOOKAHEAD;
int volatile current_index = 0;

long pending_events __attribute__((aligned(64))) = 0;
int end = 0;

__thread int my_index = 0;
__thread fallback_slot fallback_queue; //WE INITIALIZE VIA EMPTY ZERO MEMORY = { .head = NULL , .tail = NULL };

__thread unsigned me;
__thread unsigned target = -1;

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


// this have to be per_thread variable because each thread is pinned to a specific cpu. to avoid inconsistent tick took
#ifndef NUMA_BALANCING
volatile long long put_ids __attribute__((aligned(64))) = 0;
volatile long long get_ids __attribute__((aligned(64))) = 0;
/** Primary pool PPOOL */
volatile long long object_identifiers __attribute__((aligned(64))) = 0;
/** Secondary pool SPOOL */
volatile long long secondary_object_identifiers[OBJECTS] __attribute__((aligned(64))) = {[0 ... OBJECTS - 1] NO_ID};
#else
volatile long long put_ids_vector [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
volatile long long get_ids_vector [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
/** Primary pool PPOOLS */
volatile long long object_identifiers_vector [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
/** Secondary pool SPOOLS */
volatile long long secondary_object_identifiers_vector [MAX_NUMA_NODES][OBJECTS] __attribute__((aligned(64)));
#endif
volatile long long processed_IDs __attribute__((aligned(64))) = 0;
volatile unsigned long long total_worktime[NUM_SLOTS] __attribute__((aligned(64))) = { [0 ... NUM_SLOTS - 1] 0};

__thread long long _start_time = 0;
__thread long long _end_time = 0;


// this constant is core
#ifndef ALPHA
#define ALPHA (double)(0.15)
#endif


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
#ifndef NUMA_BALANCING
    for (i = 0; i < OBJECTS; i++){
        secondary_object_identifiers[i] = NO_ID; // NO_ID
    }
#else
    for (i = 0; i < MAX_NUMA_NODES; i++){
        for (j = 0; j < OBJECTS; j++){
            secondary_object_identifiers_vector[i][j] = NO_ID; // NO_ID
        }
        object_identifiers_vector[i] = 0;
    }
#endif

    for (i = 0; i < NUM_SLOTS; i++){
		total_worktime[i] = 0;
    }
    return 1;
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
#ifndef NUMA_BALANCING
    put_ids = 0;
	get_ids = 0;
    object_identifiers = 0;
#else
    for (j = 0; j < TOT_NUMA_NODES; j++){
            put_ids_vector[j] = 0;
            get_ids_vector[j] = 0;
            object_identifiers_vector[j] = 0;
        }
#endif
	processed_IDs = 0;
    // to avoid the growth of the total worktime
	total_worktime[prev_index] = 0;
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

    __sync_fetch_and_add(&total_worktime[index], queue[dest][index].event_mean_time);
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
            __sync_fetch_and_add(&total_worktime[index], queue[dest][index].event_mean_time);
            pthread_spin_unlock(&locks[dest][index].lock);


            temp = aux;
        }
    }

}


long long primary_ID_acquisition(){
    long long ID;
    long long index;
    int i;
#ifndef NUMA_BALANCING
    ID = __sync_fetch_and_add(&object_identifiers, 1);


#else
    // numa aware primary ID acquisition
    myNUMAindex = myNUMAnode;
    for (stealNUMAindex = 0; stealNUMAindex < TOT_NUMA_NODES; stealNUMAindex++){
    
        myNUMAindex = (stealNUMAindex + myNUMAnode) % TOT_NUMA_NODES;
        ID = __sync_fetch_and_add(&object_identifiers_vector[myNUMAindex], 1); 
        if (ID >= _c[myNUMAindex]){
                continue;
        }
        ID += _min[myNUMAindex];
        break;
    }
    if (stealNUMAindex >= TOT_NUMA_NODES) ID = OBJECTS; // NO_ID_AVAILABLE    
#endif

    if (ID >= OBJECTS)
    {
        return NO_ID_AVAILABLE; // this will enable the thread
        // to avoid additional call
        // to this function while
        // procesing the current epoch
    }
     /**
     *
                                     TW_i
       NEx_i × etx < (1 + α) × ----------------
                                    NUM OBJS
        *  is_ligth(ID)?                                    
     */
    if ((queue[ID][my_index].num_events * queue[ID][my_index].event_mean_time >= ALPHA * (
		total_worktime[my_index] / OBJECTS
	 )))
    {
        return ID; // the thread will simply
        // process the event of this object
    }

	//offload(ID);
#ifndef NUMA_BALANCING
    index = __sync_fetch_and_add(&put_ids, 1);
    secondary_object_identifiers[index] = ID;
#else
    index = __sync_fetch_and_add(&put_ids_vector[myNUMAindex], 1);
    secondary_object_identifiers_vector[myNUMAindex][index] = ID;
#endif
	return ID_OFFLOADED; 
	// need to eventually manage
	// offloaded objects
}

long long secondary_ID_acquisition(long long *outcome){

    long long index;
    long long ID;
#ifndef NUMA_BALANCING
    index = get_ids;
    if (index == put_ids)
#else
// secondary id acquisition label
    myNUMAindex = myNUMAnode;
    for (stealNUMAindex = 0; stealNUMAindex < TOT_NUMA_NODES; stealNUMAindex++){
        index = get_ids_vector[myNUMAindex];
        
        if (get_ids_vector[myNUMAindex] >= put_ids_vector[myNUMAindex]){
            myNUMAindex = (myNUMAindex + 1) % TOT_NUMA_NODES;
            continue;
        }
        break;
    }
    if (stealNUMAindex >= TOT_NUMA_NODES)
#endif
    {
        *outcome = NO_ID_AVAILABLE; //NO_ID_AVAILABLE;
        return NO_ID;
    }
#ifndef NUMA_BALANCING
    if (!__sync_bool_compare_and_swap(&get_ids, index, index + 1))
#else
    if (!__sync_bool_compare_and_swap(&get_ids_vector[myNUMAindex], index, index + 1))
#endif
    {
        *outcome = NEED_TO_RETRY; // NEED_TO_RETRY;
        return NO_ID;
    }

    //index++; // may be i need this index not the next
#ifndef NUMA_BALANCING
    if (secondary_object_identifiers[index] == NO_ID) // NO_ID)
#else
    if (secondary_object_identifiers_vector[myNUMAindex][index] == NO_ID) // NO_ID)
#endif
    {
        *outcome = NOT_YET_WRITTEN; // NOT_YET_WRITTEN;
        return index;
    }
    else
    {
        *outcome = WRITTEN; // WRITTEN;
#ifndef NUMA_BALANCING
        ID = secondary_object_identifiers[index];
        secondary_object_identifiers[index] = NO_ID; // NO_ID;
#else
        ID = secondary_object_identifiers_vector[myNUMAindex][index];
        secondary_object_identifiers_vector[myNUMAindex][index] = NO_ID; // NO_ID;
#endif
        return ID;
    }
}

long long ID_read_retry(long long *outcome, long long index){
    long long ID;
#ifndef NUMA_BALANCING
    if (secondary_object_identifiers[index] == NO_ID) // NO_ID)
#else
    if (secondary_object_identifiers_vector[myNUMAindex][index] == NO_ID) // NO_ID)
#endif
    {
        *outcome = NOT_YET_WRITTEN; // NOT_YET_WRITTEN;
        return NO_ID;     // NO_ID; maybe NO_ID_AVAILABLE to avoid issue
    }
    else
    {
        *outcome = WRITTEN; // WRITTEN;
#ifndef NUMA_BALANCING
        ID = secondary_object_identifiers[index];
        secondary_object_identifiers[index] = NO_ID; // NO_ID;
#else
        ID = secondary_object_identifiers_vector[myNUMAindex][index];
        secondary_object_identifiers_vector[myNUMAindex][index] = NO_ID; // NO_ID;
#endif
        return ID;
    }
}


queue_elem * queue_extract(){

    

    queue_elem * head;
    queue_elem * tail;
    queue_elem * elem;
    volatile long long ID;
    long long index;
	long long outcome;
	long long EN_i=0;
	int next_index;
    
    
start:

	if (target != NO_ID){	
        index = my_index;
	    head = &queue[target][index].head;
	    tail = &queue[target][index].tail;
        if (head->next == tail) { //the current slot is empty
            if (end){
                return NULL;
            }
            // updating the mean time of the events for the current object for next epoch
            took_tick(&_end_time);
            next_index = (index + 1) % NUM_SLOTS;
            // etx calculation
            EN_i = (_end_time - _start_time) / (queue[target][index].num_events + 1); // to avoid division by zero 0 events
            queue[target][next_index].event_mean_time = EN_i;
            __sync_fetch_and_add(&total_worktime[next_index], EN_i);
            // reset the current object events
            queue[target][index].num_events = 0;
            target = NO_ID;
            goto start;
        }
		goto process_current_epoch_events;
	}

	while (1){
		ID = primary_ID_acquisition();
		if (ID >= NO_ID_AVAILABLE) break;
		if (ID != ID_OFFLOADED){

            __sync_fetch_and_add(&processed_IDs, 1);
            target = ID;
            took_tick(&_start_time);
            goto start;
		}
	}
    long long times = 0;
	while(processed_IDs < OBJECTS){
		ID = secondary_ID_acquisition(&outcome);

		if (outcome == NO_ID_AVAILABLE){
			continue;
		} else if (outcome == NEED_TO_RETRY){
			continue;
		} else if (outcome == NOT_YET_WRITTEN){
			index = ID;
				do{
					ID = ID_read_retry(&outcome, index);
					//here you can insert any housekkeping task
					//that cna be executed while the ID to be
					//read is actually witten
				}while(outcome != WRITTEN);
		}
        //assert (ID >= 0 && ID < OBJECTS);
        __sync_fetch_and_add(&processed_IDs, 1);
        target = ID;
        took_tick(&_start_time);
        goto start;
		
	}

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

	barrier();

	my_index = current_index;

	target = NO_ID;
	//reset stuff for NUMA aware workload distribution
#ifdef NUMA_BALANCING
	myNUMAindex = myNUMAnode;
	stealNUMAindex = 0;
#endif
	fallback_check();
	goto start;

process_current_epoch_events:


	pthread_spin_lock(&locks[target][index].lock);

	if( head->next == tail) { //the current slot is empty
		// updating the mean time of the events for the current object for next epoch
		took_tick(&_end_time);
		next_index = (index + 1) % NUM_SLOTS;
		// etx calculation
		EN_i = (_end_time - _start_time) / (queue[target][index].num_events + 1); // to avoid division by zero 0 events
		queue[target][next_index].event_mean_time = EN_i;
		__sync_fetch_and_add(&total_worktime[next_index], EN_i);
        // reset the current object events
		queue[target][index].num_events = 0;
		pthread_spin_unlock(&locks[target][index].lock);
		if (end){
		       	return NULL;
		}
		target = NO_ID;

		goto start;

	}

    elem = head->next;
    head->next = elem->next;
    elem->next->prev = head;

    __sync_fetch_and_add(&pending_events,-1);

    pthread_spin_unlock(&locks[target][index].lock);

    return elem;

}
