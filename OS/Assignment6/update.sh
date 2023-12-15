#!/bin/bash

make

sudo umount -l /mnt/ez
sudo losetup --detach /dev/loop0
sudo rmmod ez
sudo mkdir -p /mnt/ez
dd bs=4096 count=400 if=/dev/zero of=./ez_disk.img
sudo losetup --find --show ./ez_disk.img
sudo ./format_disk_as_ezfs /dev/loop0
sudo insmod ez.ko
sudo mount -t ezfs /dev/loop0 /mnt/ez
