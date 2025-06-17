CC = gcc 
CFLAGS = -Wall -Wextra -I ../../include/ -I ./ -c $(_FLAGS) $(_HWSETUP)
LINKER = -Xlinker --wrap=malloc 
TARGET = intermediate-phold.o 

SRCS = application.c 

all: 
	$(CC) $(SRCS) $(CFLAGS) -o $(TARGET) $(LINKER)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

clean:
	$(RM) *.o $(TARGET)
