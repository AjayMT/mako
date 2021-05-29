#!/bin/sh
i386-elf-objdump -b binary -m i386 --start-address=$1 -D a.out
