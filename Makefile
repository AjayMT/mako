
# TODO: remove extraneous dependencies

CC := i386-elf-gcc
CFLAGS := -nostdlib -finline-small-functions -Wno-unused -Wall -Wextra -Werror -Wno-implicit-fallthrough \
          -Wno-builtin-declaration-mismatch
LD := i386-elf-ld
LDFLAGS := -T link.ld
AS := nasm
ASFLAGS := -g -Isrc/kernel/ -f elf
AR := i386-elf-ar
ARFLAGS := rcs

KERNEL_ASM_OBJECTS := $(filter-out constants.s.o,$(notdir $(patsubst %.s,%.s.o,$(wildcard src/kernel/*.s))))
KERNEL_OBJECTS := $(notdir $(patsubst %.c,%.o,$(wildcard src/kernel/*.c)))

LIBC_OBJECTS := $(notdir $(patsubst %.c,%.o,$(wildcard src/libc/*.c)))

DEPS := nanoc lua doomgeneric
APPS := dex xed pie img
BIN := $(notdir $(patsubst %.c,%,$(wildcard src/bin/*.c)))

GRUB_MKRESCUE := grub-mkrescue
ifeq ($(shell uname -s), Darwin)
	GRUB_MKRESCUE := i386-elf-grub-mkrescue
endif

export BUILD_ROOT := $(PWD)

all: mako.iso hda.img

# ==== Kernel ISO ====

mako.iso: $(KERNEL_ASM_OBJECTS) $(KERNEL_OBJECTS)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) $(KERNEL_ASM_OBJECTS) -o kernel.elf
	mv kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o mako.iso iso

$(KERNEL_OBJECTS): $(wildcard src/kernel/*.c) $(wildcard src/kernel/*.h) $(wildcard src/common/*)
	$(CC) $(CFLAGS) -ffreestanding -c src/kernel/$(basename $@).c -o $@

$(KERNEL_ASM_OBJECTS): $(wildcard src/kernel/*.s)
	$(AS) $(ASFLAGS) src/kernel/$(basename $@) -o $@

# ==== /Kernel ISO ====

# ==== HDD Image ===

hda.img: $(APPS) $(BIN) $(DEPS) libnanoc.a
	mv libnanoc.a sysroot/lib
	mv $(APPS) sysroot/apps
	mv $(BIN) sysroot/bin
	mv nanoc lua sysroot/bin
	mv doomgeneric sysroot/apps
	./gen-hda.sh

# $(DEPS)
lua: $(shell find deps/lua -type f) libc.a crt0.o crti.o crtn.o
	$(MAKE) -C deps/lua generic
	mv deps/lua/src/lua .
nanoc: $(wildcard deps/nanoc/*) libc.a crt0.o crti.o crtn.o
	$(CC) $(CFLAGS) -Isrc/libc/ deps/nanoc/nanoc.c crt0.o crti.o crtn.o libc.a -lgcc -o nanoc
doomgeneric: $(shell find deps/doomgeneric -type f) libui.a libc.a crt0.o crti.o crtn.o
	$(MAKE) -C deps/doomgeneric/doomgeneric
	mv deps/doomgeneric/doomgeneric/doomgeneric .

# Need two rules for $(APPS) since they have different dependencies
dex xed pie: $(wildcard src/apps/*) libc.a libui.a crt0.o crti.o crtn.o
	$(CC) $(CFLAGS) -Isrc/libc/ -Isrc/libui/ src/apps/$@.c src/apps/font_monaco.c \
	src/apps/text_render.c src/apps/scancode.c crt0.o crti.o crtn.o libc.a libui.a -lgcc -o $@
img: src/apps/img.c src/apps/lodepng.c src/apps/lodepng.h src/apps/scancode.h libc.a libui.a crt0.o crti.o crtn.o
	$(CC) $(CFLAGS) -Isrc/libc/ -Isrc/libui/ src/apps/img.c src/apps/lodepng.c \
	crt0.o crti.o crtn.o libc.a libui.a -lgcc -o $@

$(BIN): $(wildcard src/bin/*.c) libc.a crt0.o crti.o crtn.o
	$(CC) $(CFLAGS) -Isrc/libc/ src/bin/$@.c crt0.o crti.o crtn.o libc.a -lgcc -o $@

libnanoc.a: src/nanoc.c
	$(CC) $(CFLAGS) -ffreestanding -c src/nanoc.c -o nanoc.o
	$(AR) $(ARFLAGS) libnanoc.a nanoc.o

libui.a: $(wildcard src/libui/*) $(wildcard src/common/*) libc.a
	$(CC) $(CFLAGS) -ffreestanding -c src/libui/ui.c -o libui.o # Name `ui.o` conflicts with kernel object
	$(AR) $(ARFLAGS) libui.a libui.o

libc.a: $(LIBC_OBJECTS) setjmp.o
	$(AR) $(ARFLAGS) libc.a $(LIBC_OBJECTS) setjmp.o

crt0.o crti.o crtn.o: $(wildcard src/libc/*.s)
	$(AS) $(ASFLAGS) src/libc/$(basename $@).s -o $@

$(LIBC_OBJECTS): $(wildcard src/libc/*.c) $(wildcard src/libc/*.h) $(wildcard src/common/*)
	$(CC) $(CFLAGS) -ffreestanding -c src/libc/$(basename $@).c -o $@

setjmp.o: src/libc/setjmp.s
	$(AS) $(ASFLAGS) src/libc/setjmp.s -o setjmp.o

# ==== /HDD Image ====

.PHONY: qemu
qemu: mako.iso
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso -m 256M \
	                 -drive format=raw,file=hda.img -d cpu_reset

.PHONY: clean
clean:
	rm -rf *.o *.a iso/boot/kernel.elf mako.iso com1.out              \
	       sysroot/lib/* sysroot/bin/* sysroot/apps/* lua c4 doomgeneric nanoc   \
	       $(APPS) $(BIN) $(DEPS) hda.img hda.tar ustar_image
