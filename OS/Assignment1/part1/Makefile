CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=


OBJECTS := shell.o

all: w4118_sh


w4118_sh: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f w4118_sh *.o

.PHONY: clean
