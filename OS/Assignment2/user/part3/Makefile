CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=

OBJECTS := test.o

all: test


test: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

clean:
	rm -f test *.o

.PHONY: clean