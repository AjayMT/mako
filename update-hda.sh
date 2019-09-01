#!/bin/sh

mount hda.img /mnt
rm -fr /mnt/*
cp -r /sysroot/* /mnt
umount /mnt
