CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=
LDLIBS := -lpthread

OBJECTS := foo.o

all: foo

foo: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $(OBJECTS)

clean:
	rm -f foo *.o

.PHONY: clean