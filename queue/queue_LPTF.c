#include <queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setup.h>
#include "memory.h"
#include <sys/signal.h>

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
long long put_ID __attribute__((aligned(64))) = 0;
long long get_ID __attribute__((aligned(64))) = 0;
long long available_ID __attribute__((aligned(64))) = 0;
long long secondary_IDs[OBJECTS] __attribute__((aligned(64))) = {[0 ... OBJECTS - 1] NO_ID};
#else
long long put_IDs [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
long long get_IDs [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
long long available_IDs [MAX_NUMA_NODES] __attribute__((aligned(64))) = { [0 ... MAX_NUMA_NODES-1] 0};
long long secondary_IDs [MAX_NUMA_NODES][OBJECTS] __attribute__((aligned(64)));
#endif
long long processed_IDs __attribute__((aligned(64))) = 0;
unsigned long long total_worktime[NUM_SLOTS] __attribute__((aligned(64))) = { [0 ... NUM_SLOTS - 1] 0};

__thread long long _start_time = 0;
__thread long long _end_time = 0;
__thread long long _ID_offloaded = -1;

// this constant is core
#define ALPHA (double)(1.8)
const double one_plus_alpha = (1 + ALPHA);

void whoami(unsigned my_id){
    AUDIT printf("just audit whoami: %u\n",my_id);
    me = my_id;
    _c = getcounter();
    _min = getmin();
    _max = getmax();
    myNUMAindex = myNUMAnode = get_NUMAnode();
    stealNUMAindex = 0;
    TOT_NUMA_NODES = get_totNUMAnodes();
    printf("thread %u called %s\n", me, __func__);
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
        secondary_IDs[i] = NO_ID; // NO_ID
    }
#else
    for (i = 0; i < MAX_NUMA_NODES; i++){
        for (j = 0; j < OBJECTS; j++){
            secondary_IDs[i][j] = NO_ID; // NO_ID
        }
        available_IDs[i] = 0;
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
    put_ID = 0;
	get_ID = 0;
    available_ID = 0;
#else
    for (j = 0; j < TOT_NUMA_NODES; j++){
            put_IDs[j] = 0;
            get_IDs[j] = 0;
            available_IDs[j] = 0;
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
// TODO: manage the etx mean time addition when a new event is inserted
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


// TODO: check if this is NUMA aware
long long primary_ID_acquisition(){
    long long ID;
#ifndef NUMA_BALANCING
    ID = __sync_fetch_and_add(&available_ID, 1);
#else
// primary id acquisition label
retry_numa_pa:
    // numa aware primary ID acquisition
    ID = __sync_fetch_and_add(&available_IDs[myNUMAindex], 1); 
    
    if (ID >= _c[myNUMAindex]){
			if(stealNUMAindex < TOT_NUMA_NODES){
				stealNUMAindex++;
				myNUMAindex = (myNUMAindex+1)%TOT_NUMA_NODES;
				goto retry_numa_pa;
			}
			else ID = OBJECTS;
		}
		else{
			ID += _min[myNUMAindex];
		}
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
    if (!(queue[ID][my_index].num_events * queue[ID][my_index].event_mean_time < one_plus_alpha * (
		total_worktime[my_index] / OBJECTS
	 )))
    {

        return ID; // the thread will simply
        // process the event of this object
    }

	//offload(ID);
#ifndef NUMA_BALANCING
    _ID_offloaded = __sync_fetch_and_add(&put_ID, 1);
    secondary_IDs[_ID_offloaded] = ID;
#else
    _ID_offloaded = __sync_fetch_and_add(&put_IDs[myNUMAindex], 1);
    secondary_IDs[myNUMAindex][_ID_offloaded] = ID;
#endif
	return _ID_offloaded; //  the thread knows it will by using a per_thread variable
	// need to eventually manage
	// offloaded objects
}

long long secondary_ID_acquisition(long long *outcome){

    long long index;
    long long ID;
#ifndef NUMA_BALANCING
    index = get_ID;
    if (index >= put_ID)
#else
// secondary id acquisition label
retry_numa_sa:
    // numa aware secondary ID acquisition
    index = get_IDs[myNUMAindex];
    if (stealNUMAindex < TOT_NUMA_NODES){
        if (index == put_IDs[myNUMAindex]){
            myNUMAindex = (myNUMAindex + 1) % TOT_NUMA_NODES;
            stealNUMAindex++;       
            goto retry_numa_sa;         
        }
    }else
#endif
    {
        *outcome = NO_ID_AVAILABLE; //NO_ID_AVAILABLE;
        return -1;
    }
#ifndef NUMA_BALANCING
    if (!__sync_bool_compare_and_swap(&get_ID, index, index + 1))
#else
    if (!__sync_bool_compare_and_swap(&get_IDs[myNUMAindex], index, index + 1))
#endif
    {
        *outcome = NEED_TO_RETRY; // NEED_TO_RETRY;
        return -1;
    }

    //index++; // may be i need this index not the next
#ifndef NUMA_BALANCING
    if (secondary_IDs[index] == NO_ID) // NO_ID)
#else
    if (secondary_IDs[myNUMAindex][index] == NO_ID) // NO_ID)
#endif
    {
        *outcome = NOT_YET_WRITTEN; // NOT_YET_WRITTEN;
        return index;
    }
    else
    {
        *outcome = WRITTEN; // WRITTEN;
#ifndef NUMA_BALANCING
        ID = secondary_IDs[index];
        secondary_IDs[index] = NO_ID; // NO_ID;
#else
        ID = secondary_IDs[myNUMAindex][index];
        secondary_IDs[myNUMAindex][index] = NO_ID; // NO_ID;
#endif
        return ID;
    }
}

long long ID_read_retry(long long *outcome, long long index){
    long long ID;
#ifndef NUMA_BALANCING
    if (secondary_IDs[index] == NO_ID) // NO_ID)
#else
    if (secondary_IDs[myNUMAindex][index] == NO_ID) // NO_ID)
#endif
    {
        *outcome = NOT_YET_WRITTEN; // NOT_YET_WRITTEN;
        return NO_ID_AVAILABLE;     // NO_ID; maybe NO_ID_AVAILABLE to avoid issue
    }
    else
    {
        *outcome = WRITTEN; // WRITTEN;
#ifndef NUMA_BALANCING
        ID = secondary_IDs[index];
        secondary_IDs[index] = NO_ID; // NO_ID;
#else
        ID = secondary_IDs[myNUMAindex][index];
        secondary_IDs[myNUMAindex][index] = NO_ID; // NO_ID;
#endif
        return ID;
    }
}


queue_elem * queue_extract(){

    int index;

    queue_elem * head;
    queue_elem * tail;
    queue_elem * elem;
    long long ID;
	long long outcome;
	long long EN_i=0;
	int next_index;
    
    
start:

	if (target != NO_ID){
		goto workload_process;
	}

	while (1){
		ID = primary_ID_acquisition();
		if (ID == NO_ID_AVAILABLE) break;
		if (ID != _ID_offloaded){

			if (ID < OBJECTS){
                __sync_fetch_and_add(&processed_IDs, 1);
				target = ID;
				//_to_process = 1;
				took_tick(&_start_time);
				goto workload_process;
			}
			printf("ERROR: never be here\n");
		}
	}

#ifdef NUMA_BALANCING
    // reset numan aware indexes for secondary ID acqusition
    myNUMAindex = myNUMAnode;
    stealNUMAindex = 0;
#endif

	while(processed_IDs < OBJECTS){
		ID = secondary_ID_acquisition(&outcome);

		if (outcome == NO_ID_AVAILABLE){
			break;
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

		if (ID < OBJECTS){
            __sync_fetch_and_add(&processed_IDs, 1);
			target = ID;
			//_to_process = 1;
			took_tick(&_start_time);
			goto workload_process;
		}
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

	target = -1;
	_ID_offloaded = -1;
	//reset stuff for NUMA aware workload distribution
#ifdef NUMA_BALANCING
	myNUMAindex = myNUMAnode;
	stealNUMAindex = 0;
#endif
	fallback_check();
	goto start;

workload_process:

	index = my_index;
	head = &queue[target][index].head;
	tail = &queue[target][index].tail;
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
