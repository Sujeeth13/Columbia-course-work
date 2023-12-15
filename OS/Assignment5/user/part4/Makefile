CC := gcc
CFLAGS := -Wall -Werror -g
LDFLAGS :=

INSPECTOR_OBJECTS := inspector.o
TARGET_OBJECTS := target.o

all: inspector target

inspector: $(INSPECTOR_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(INSPECTOR_OBJECTS)

target: $(TARGET_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TARGET_OBJECTS)

clean:
	rm -f inspector target *.o

.PHONY: all clean
