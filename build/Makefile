# ====== Configuration ======
FLAGS     = #-DNUMA_BALANCING -DNUMA_UBIQUITOUS
AFLAGS    = -DSYSCALL_NUMBER=134
HWSETUP   = -DMEM_NODES=2
QUEUE_IMPL ?= DEFAULT
NUMA ?= 0
UNBALANCE ?= 0
NUM_BARRIER ?= -1

# ====== Queue source selection ======
ifeq ($(QUEUE_IMPL),LPTF)
  QUEUE_SRC = ../queue/queue_LPTF.c
  OUTPUT_NAME = -LPTF
else
  QUEUE_SRC = ../queue/queue.c
  OUTPUT_NAME = -ORIGIN
endif

ifeq ($(NUMA), 1)
	FLAGS += -DNUMA_BALANCING
	OUTPUT_NAME := $(OUTPUT_NAME)-NUMA
endif

ifeq ($(UNBALANCE), 1)
	FLAGS += -DUNBALANCE
	OUTPUT_NAME := $(OUTPUT_NAME)-UNBALANCE
else
	OUTPUT_NAME := $(OUTPUT_NAME)-BALANCE
endif

ifneq ($(NUM_BARRIER), -1)
	FLAGS += -DMEAN_TIME_NUM=$(NUM_BARRIER)
endif

# ====== Optional build-time parameters ======
THREADS ?= 40
LOOKAHEAD ?= 1.0
OBJECTS ?= 1024
ALPHA ?= 0.1
# H_LOAD
HL ?= 2.0
# HOTSPOTS
HP ?= 0.1

FLAGS += -DALPHA=$(ALPHA)
FLAGS += -DTHREADS=$(THREADS)
FLAGS += -DLOOKAHEAD=$(LOOKAHEAD)
FLAGS += -DOBJECTS=$(OBJECTS)
FLAGS += -DH_LOAD=$(HL)
FLAGS += -DHOTSPOTS=$(HP)


# ====== Targets ======
all: runtime
	@echo "The runtime environment of PARSIR has been compiled - object files are available in the lib directory"

runtime:
	mkdir -p ../lib	
	mkdir -p ../bin
	cd ../scripts && make get-HW-info
	gcc -c -O3 $(QUEUE_SRC) -o ../lib/queue.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I ../include $(FLAGS) $(HWSETUP)
	gcc -c -O3 ../syscalls/syscalls.c -o ../lib/syscalls.o -I ../include $(FLAGS) $(AFLAGS) $(HWSETUP)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/syscalls.o -I ../include -o ../lib/PARSIR-runtime.o

LKM:
	cd ../PTE-entry-switcher && make && make mount
	dmesg | grep acquiring | grep SCTH | tail -1

rm-LKM:
	sudo rmmod the_pte-entry-switcher

pcs: runtime
	make -C ../models/pcs _FLAGS="$(FLAGS)" _HWSETUP="$(HWSETUP)"
	gcc -o ../bin/PARSIR-simulator-pcs$(OUTPUT_NAME) ../lib/PARSIR-runtime.o -I ../include -lpthread -lnuma ../models/pcs/pcs.o -lm

highway: runtime
	make -C ../models/highway _FLAGS="$(FLAGS)" _HWSETUP="$(HWSETUP)"
	gcc -g -o ../bin/PARSIR-simulator-highway$(OUTPUT_NAME) ../lib/PARSIR-runtime.o -I ../include -lpthread -lnuma ../models/highway/highway.o

phold: runtime
	make -C ../models/phold _FLAGS="$(FLAGS)" _HWSETUP="$(HWSETUP)"
	gcc -o ../bin/PARSIR-simulator-phold$(OUTPUT_NAME) ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma ../models/phold/phold.o