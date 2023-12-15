CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=

.PHONY: default
default: test seven_states

test: test.c
	$(CC) $(CFLAGS) -o test test.c

seven_states: seven_states.c
	$(CC) $(CFLAGS) -o seven_states seven_states.c

.PHONY: clean
clean:
	rm -f test seven_states