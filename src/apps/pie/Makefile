
CC = i386-elf-gcc
LIBS=${BUILD_ROOT}/sysroot/usr/lib/crt0.o ${BUILD_ROOT}/sysroot/usr/lib/crti.o ${BUILD_ROOT}/sysroot/usr/lib/crtn.o ${BUILD_ROOT}/sysroot/usr/lib/libc.a ${BUILD_ROOT}/sysroot/usr/lib/libui.a -lgcc

$(out): pie.c font_monaco.c text_render.c scancode.c
	$(CC) -Wno-builtin-declaration-mismatch -nostdlib pie.c font_monaco.c text_render.c scancode.c $(LIBS) -o $(out) -I${BUILD_ROOT}/sysroot/usr/include
