
CC = i386-elf-gcc
CFLAGS = -ffreestanding -nostdlib -nostdinc -c
AS = nasm
ASFLAGS = -f elf32
LD = i386-elf-ld

all: test

test: test.o crt0.o
	$(LD) test.o crt0.o -o test

test.o: test.c
	$(CC) $(CFLAGS) test.c -o test.o

crt0.o:
	$(AS) $(ASFLAGS) crt0.s -o crt0.o

.PHONY: clean
clean:
	rm -fr test *.o
