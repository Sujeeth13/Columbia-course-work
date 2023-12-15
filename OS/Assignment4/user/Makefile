CC=gcc
CFLAGS=-Wall -g

.PHONY: all clean

all: fibonacci fibonacci_tail

fibonacci: fibonacci.c
	$(CC) $(CFLAGS) -o fibonacci fibonacci.c

fibonacci_tail: fibonacci_tail.c
	$(CC) $(CFLAGS) -o fibonacci_tail fibonacci_tail.c

clean:
	rm -f fibonacci fibonacci_tail