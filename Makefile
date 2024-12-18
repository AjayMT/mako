
# TODO: remove extraneous dependencies

CC := i386-elf-gcc
CFLAGS := -g -nostdlib -finline-small-functions -Wno-unused -Wall -Wextra -Werror -Wno-implicit-fallthrough \
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
CRT_OBJECTS := crt0.o crti.o crtn.o

PORTS := nanoc lua doomgeneric
APPS := $(notdir $(patsubst %.c,%,$(wildcard src/apps/*.c)))
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
	cp kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o mako.iso iso

$(KERNEL_OBJECTS): $(wildcard src/kernel/*.c) $(wildcard src/kernel/*.h) $(wildcard src/common/*)
	$(CC) $(CFLAGS) -ffreestanding -c src/kernel/$(basename $@).c -o $@

$(KERNEL_ASM_OBJECTS): $(wildcard src/kernel/*.s)
	$(AS) $(ASFLAGS) src/kernel/$(basename $@) -o $@

# ==== /Kernel ISO ====

# ==== HDD Image ===

hda.img: $(APPS) $(BIN) $(PORTS) libnanoc.a $(shell find sysroot -type f)
	cp libnanoc.a sysroot/lib
	cp $(APPS) sysroot/apps
	cp $(BIN) sysroot/bin
	cp nanoc lua sysroot/bin
	cp doomgeneric sysroot/apps
	./gen-hda.sh

# $(PORTS)
lua: $(shell find ports/lua -type f) libc.a $(CRT_OBJECTS)
	$(MAKE) -C ports/lua generic
	cp ports/lua/src/lua .
nanoc: $(wildcard ports/nanoc/*) libc.a $(CRT_OBJECTS)
	$(CC) $(CFLAGS) -Isrc/libc/ ports/nanoc/nanoc.c $(CRT_OBJECTS) libc.a -lgcc -o nanoc
doomgeneric: $(shell find ports/doomgeneric -type f) libui.a libc.a $(CRT_OBJECTS)
	$(MAKE) -C ports/doomgeneric/doomgeneric
	cp ports/doomgeneric/doomgeneric/doomgeneric .

$(APPS): $(wildcard src/apps/*) libc.a libui.a $(CRT_OBJECTS)
	$(CC) $(CFLAGS) -Isrc/libc/ -Isrc/libui/ src/apps/$@.c $(CRT_OBJECTS) libc.a libui.a -lgcc -o $@

# wallpaper depends on libui.a
$(filter-out wallpaper,$(BIN)): $(wildcard src/bin/*.c) libc.a $(CRT_OBJECTS)
	$(CC) $(CFLAGS) -Isrc/libc/ src/bin/$@.c $(CRT_OBJECTS) libc.a -lgcc -o $@

wallpaper: src/bin/wallpaper.c libc.a libui.a $(CRT_OBJECTS)
	$(CC) $(CFLAGS) -Isrc/libc/ -Isrc/libui/ src/bin/$@.c $(CRT_OBJECTS) libc.a libui.a -lgcc -o $@

libnanoc.a: src/nanoc.c $(wildcard src/common/*)
	$(CC) $(CFLAGS) -ffreestanding -c src/nanoc.c -o nanoc.o
	$(AR) $(ARFLAGS) libnanoc.a nanoc.o

libui.a: $(wildcard src/libui/*) $(wildcard src/common/*) libc.a
	$(CC) $(CFLAGS) -ffreestanding -c src/libui/ui.c -o libui.o # Name `ui.o` conflicts with kernel object
	$(AR) $(ARFLAGS) libui.a libui.o

libc.a: $(LIBC_OBJECTS) setjmp.o
	$(AR) $(ARFLAGS) libc.a $(LIBC_OBJECTS) setjmp.o

$(CRT_OBJECTS): $(wildcard src/libc/*.s)
	$(AS) $(ASFLAGS) src/libc/$(basename $@).s -o $@

$(LIBC_OBJECTS): $(wildcard src/libc/*.c) $(wildcard src/libc/*.h) $(wildcard src/common/*)
	$(CC) $(CFLAGS) -ffreestanding -c src/libc/$(basename $@).c -o $@

setjmp.o: src/libc/setjmp.s
	$(AS) $(ASFLAGS) src/libc/setjmp.s -o setjmp.o

# ==== /HDD Image ====

.PHONY: qemu
qemu: mako.iso hda.img
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso -m 256M -monitor stdio \
	                 -drive format=raw,file=hda.img

.PHONY: clean
clean:
	rm -rf *.elf *.o *.a iso/boot/kernel.elf mako.iso com1.out                 \
	       sysroot/lib/* sysroot/bin/* sysroot/apps/* lua c4 doomgeneric nanoc \
	       $(APPS) $(BIN) $(PORTS) hda.img hda.tar ustar_image png2wp \
	       png2bitmap font_compiler
