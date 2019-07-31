#!/bin/sh

sudo fuse-ext2 -o rw+ hda.img /mnt
sudo rm -rf /mnt/*
sudo cp -r sysroot/* /mnt/
diskutil unmount /mnt
