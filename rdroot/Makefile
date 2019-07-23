
CC = i386-elf-gcc
CFLAGS = -ffreestanding -nostdlib -I../src/ \
         -I../src/libc/h -c
AS = nasm
ASFLAGS = -f elf32
LD = i386-elf-ld
LDFLAGS = -L.. -L$(dir $(shell $(CC) -print-file-name=libgcc.a))
# CRTBEGIN_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtbegin.o)
# CRTEND_OBJ:=$(shell $(CC) $(CFLAGS) -print-file-name=crtend.o)
CRTBEGIN_OBJ:=
CRTEND_OBJ:=

all: test test2

test: test.o
	$(LD) $(LDFLAGS) ../crt0.o ../crti.o $(CRTBEGIN_OBJ) \
	                 test.o $(CRTEND_OBJ) ../crtn.o -o test -lc -lgcc

test2: test2.o
	$(LD) $(LDFLAGS) ../crt0.o ../crti.o $(CRTBEGIN_OBJ) \
	                 test2.o $(CRTEND_OBJ) ../crtn.o -o test2 -lc -lgcc

test.o: test.c
	$(CC) $(CFLAGS) test.c -o test.o

test2.o: test2.c
	$(CC) $(CFLAGS) test2.c -o test2.o

.PHONY: clean
clean:
	rm -fr test test2 *.o
