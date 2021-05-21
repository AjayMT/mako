#!/bin/sh

cc -o ustar_image tools/ustar_image.c
tar cf hda.tar sysroot
./ustar_image hda.tar
dd if=/dev/zero of=hda.img bs=1k count=150000
dd if=hda.tar of=hda.img conv=notrunc
