CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=


OBJECTS := shell2.o

all: w4118_sh2


w4118_sh2: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^

clean:
	rm -f w4118_sh2 *.o

.PHONY: clean
