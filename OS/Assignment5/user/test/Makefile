# pretty standard Makefile to build "test"

CC := gcc
CFLAGS := -Wall -Werror -g

OBJECTS := test.o

all: test

test: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS) test

.PHONY: clean
