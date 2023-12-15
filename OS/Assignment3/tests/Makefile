CC = gcc
CFLAGS = -pthread

.PHONY: default
default: test_simple test_robust test_basic test_clear_wakeup test_get_wait test_get_wait_multi test_pressure

test_simple: test_simple.c
	$(CC) $(CFLAGS) -o test_simple test_simple.c

test_robust: test_robust.c
	$(CC) $(CFLAGS) -o test_robust test_robust.c

test_basic: test_basic.c
	$(CC) $(CFLAGS) -o test_basic test_basic.c

test_clear_wakeup: test_clear_wakeup.c
	$(CC) $(CFLAGS) -o test_clear_wakeup test_clear_wakeup.c

test_get_wait: test_get_wait.c
	$(CC) $(CFLAGS) -o test_get_wait test_get_wait.c

test_get_wait_multi: test_get_wait_multi.c
	$(CC) $(CFLAGS) -o test_get_wait_multi test_get_wait_multi.c

test_pressure: test_pressure.c
	$(CC) $(CFLAGS) -o test_pressure test_pressure.c

.PHONY: clean
clean:
	rm -f test_simple test_robust test_basic test_clear_wakeup test_get_wait test_get_wait_multi test_pressure
