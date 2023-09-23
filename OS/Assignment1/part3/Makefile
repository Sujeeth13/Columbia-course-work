ARCH=$(shell uname -m)

CFLAGS=-target $(ARCH)-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone

LDFLAGS=-target $(ARCH)-unknown-windows -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application -fuse-ld=lld-link

all: compile link-$(ARCH) img-$(ARCH)

compile:
	clang $(CFLAGS) -c -o main.o main.c

link-aarch64:
	clang $(LDFLAGS) -o BOOTAA64.EFI main.o

link-x86_64:
	clang $(LDFLAGS) -o BOOTX64.EFI main.o

img-shared:
	rm -f disk.img
	dd if=/dev/zero of=disk.img bs=1k count=1440
	mformat -i disk.img -f 1440 ::
	mmd -i disk.img ::/EFI
	mmd -i disk.img ::/EFI/BOOT

img-aarch64: img-shared
	mcopy -i disk.img BOOTAA64.EFI ::/EFI/BOOT

img-x86_64: img-shared
	mcopy -i disk.img BOOTX64.EFI ::/EFI/BOOT

.PHONY: clean
clean:
	rm -f *.o *.EFI *.img
