
#ifndef __QUEUE
#define QUEUE


#define WORKLOAD_DISTRIBUTION
#define BARRIER_TIMER

#include <pthread.h>
#include <setup.h>

#define NUM_SLOTS (2000)
#define SLOT_LEN (LOOKAHEAD)
#define QUEUE_TIME_INTERVAL (SLOT_LEN * NUM_SLOTS)
typedef struct _queue_elem{
    int destination;
    double timestamp;
    struct _queue_elem * next;
    struct _queue_elem * prev;
} queue_elem;

typedef struct _slot{
    queue_elem  head;
    queue_elem  tail;
#ifdef WORKLOAD_DISTRIBUTION
    // number of events in the slot
	unsigned long long __attribute__((aligned(64))) num_events;
	unsigned long long __attribute__((aligned(64))) mean_time;
#endif
} slot;

#ifdef WORKLOAD_DISTRIBUTION


enum OUTCOME {
    NO_ID_AVAILABLE = OBJECTS,
    NO_ID = -1,
    WRITTEN = 0,
    NEED_TO_RETRY = 2,
    NOT_YET_WRITTEN = 3
};

/**
 * MACRO: takes the current CPU tick and stores it in the pointer
 * @param ptr pointer to the location where the tick will be stored
 */
#define took_tick(ptr) asm volatile( \
    "rdtscp\n\t" \
    "shl $32, %%rdx\n\t" \
    "or %%rax, %%rdx\n\t" \
    "mov %%rdx, (%0)" \
    : /* no output */ \
    : "r"(ptr) \
    : "rax", "rdx", "rcx" )

#endif

typedef struct _fallbacks_lot{
    queue_elem*  head;
    queue_elem*  tail;
} fallback_slot;

typedef union _lock_buffer{
    pthread_spinlock_t lock;
    char buff[64];
} __attribute__((packed)) lock_buffer;

int queue_init(void);
void whoami(unsigned);
int queue_insert(queue_elem * elem);
queue_elem * queue_extract(void);
int barrier(void);
#ifdef BARRIER_TIMER
int barrier_timer(void);
#endif
#define AUDIT if(0)

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#endif
