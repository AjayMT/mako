#!/bin/sh

fuse-ext2 -o rw+ hda.img mnt
sudo rm -fr mnt/*
sudo cp -r sysroot/* mnt
if [[ $OSTYPE == "darwin"* ]]; then
  diskutil unmount mnt
else
  umount mnt
fi
