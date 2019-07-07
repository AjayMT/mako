
CC = i386-elf-gcc
CFLAGS = -ffreestanding -nostdlib -lgcc -I../src/ -c
AS = nasm
ASFLAGS = -f elf32
LD = i386-elf-ld

all: test test2

test: test.o crt0.o
	$(LD) test.o crt0.o -o test

test2: test2.o crt0.o
	$(LD) test2.o crt0.o -o test2

test.o: test.c
	$(CC) $(CFLAGS) test.c -o test.o

test2.o: test2.c
	$(CC) $(CFLAGS) test2.c -o test2.o

crt0.o:
	$(AS) $(ASFLAGS) crt0.s -o crt0.o

.PHONY: clean
clean:
	rm -fr test test2 *.o
