
CC = i386-elf-gcc
LIBS=${BUILD_ROOT}/sysroot/usr/lib/crt0.o ${BUILD_ROOT}/sysroot/usr/lib/crti.o ${BUILD_ROOT}/sysroot/usr/lib/crtn.o ${BUILD_ROOT}/sysroot/usr/lib/libc.a -lgcc

c4: c4.c
	$(CC) -nostdlib -w c4.c $(LIBS) -o c4 -I${BUILD_ROOT}/sysroot/usr/include

.PHONY: clean
clean:
	rm -f c4
