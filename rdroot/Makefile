
all: test test2 test3

test: test.s
	nasm -f elf test.s -o test.o
	i386-elf-ld -T link.ld test.o -o test

test2: test2.s
	nasm -f elf test2.s -o test2.o
	i386-elf-ld -T link.ld test2.o -o test2

test3: test3.s
	nasm -f elf32 test3.s -o test3.o
	i386-elf-ld test3.o -o test3

.PHONY: clean
clean:
	rm -fr test test.o test2 test2.o test3 test3.o
