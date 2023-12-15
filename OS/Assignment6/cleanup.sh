#!/bin/bash

sudo umount -l /mnt/ez
sudo losetup --detach /dev/loop0
sudo rmmod ez
