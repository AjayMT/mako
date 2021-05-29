CC=i386-elf-gcc
LIBS=${BUILD_ROOT}/sysroot/usr/lib/crt0.o ${BUILD_ROOT}/sysroot/usr/lib/crti.o ${BUILD_ROOT}/sysroot/usr/lib/crtn.o ${BUILD_ROOT}/sysroot/usr/lib/libc.a -lgcc

nanoc: nanoc.c
	$(CC) -Wno-builtin-declaration-mismatch -nostdlib nanoc.c $(LIBS) -o nanoc -I${BUILD_ROOT}/sysroot/usr/include

archive.a: test/archive.c
	i386-elf-gcc -c test/archive.c -o archive.o
	i386-elf-ar r archive.a archive.o
 
.PHONY: clean
clean:
	rm -fr nanoc a.out archive.o archive.a
