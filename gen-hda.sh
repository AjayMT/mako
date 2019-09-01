#!/bin/sh

dd if=/dev/zero of=hda.img bs=1k count=100000 > /dev/zero
mkfs -t ext2 -i 1024 -b 1024 -F hda.img > /dev/zero
fdisk hda.img <<EOF
x
c
10
h
16
s
63
r
n
p
1
a
1
w
EOF
