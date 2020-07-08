#!/bin/sh

dd if=/dev/zero of=hda.img bs=1k count=150000 > /dev/zero
mke2fs -i 1024 -b 1024 -F hda.img > /dev/zero

