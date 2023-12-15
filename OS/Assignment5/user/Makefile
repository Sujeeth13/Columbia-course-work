CC := gcc
CFLAGS := -Wall -Werror -g -O0
LDFLAGS :=

#OBJECTS := inspector.o

all: inspector target content-inspector randfile

test: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

randfile:
	dd bs=1024 count=256 </dev/urandom >randfile

clean:
	rm -f target content-inspector inspector *.o randfile

.PHONY: clean
