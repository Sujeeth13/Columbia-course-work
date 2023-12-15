obj-m += ez.o

all: kmod format_disk_as_ezfs

format_disk_as_ezfs: CC = gcc
format_disk_as_ezfs: CFLAGS = -g -Wall

PHONY += kmod
kmod:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

PHONY += clean
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f format_disk_as_ezfs

.PHONY: $(PHONY)
