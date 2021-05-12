
CC = i386-elf-gcc
LIBS=${BUILD_ROOT}/sysroot/usr/lib/crt0.o ${BUILD_ROOT}/sysroot/usr/lib/crti.o ${BUILD_ROOT}/sysroot/usr/lib/crtn.o ${BUILD_ROOT}/sysroot/usr/lib/libc.a -lgcc

$(out): pwd.c
	$(CC) -nostdlib pwd.c $(LIBS) -o $(out) -I${BUILD_ROOT}/sysroot/usr/include
