
CC = i386-elf-gcc
CFLAGS = -g -nostdlib -fstack-protector-explicit -finline-small-functions \
         -ffreestanding -Wno-unused -Wall -Wextra -Werror \
         -Wno-implicit-fallthrough -lgcc -I${PWD}/src/ -c
LD = i386-elf-ld
LDFLAGS = -T link.ld
AS = nasm
ASFLAGS = -g -I${PWD}/src/ -f elf
BUILD_ROOT=${PWD}
DRIVER_OBJECTS = io.o serial.o keyboard.o ata.o \
                 pci.o
ASM_OBJECTS = boot.s.o gdt.s.o idt.s.o interrupt.s.o paging.s.o \
              tss.s.o process.s.o syscall.s.o klock.s.o         \
              ringbuffer.s.o fpu.s.o
OBJECTS = boot.o gdt.o idt.o pic.o interrupt.o paging.o pmm.o  \
          debug.o util.o kheap.o fs.o ext2.o ds.o rd.o tss.o   \
          process.o pit.o elf.o syscall.o klock.o ringbuffer.o \
          pipe.o fpu.o rtc.o ui.o ustar.o
LIBS = libc.a libui.a libnanoc.a
APPS = dex xed pie img
BIN = init pwd ls read
GRUB_MKRESCUE = grub-mkrescue
ifeq ($(shell uname -s), Darwin)
	GRUB_MKRESCUE = i386-elf-grub-mkrescue
endif
export

all: mako.iso

.PHONY: user
user: deps $(APPS) $(BIN)

.PHONY: deps
deps: sysroot lua c4 doomgeneric nanoc
	cp lua sysroot/bin
	cp c4 sysroot/bin
	cp doomgeneric sysroot/apps
	cp nanoc sysroot/bin

c4: $(shell find src/libc -type f) $(shell find deps/c4 -type f)
	$(MAKE) -C deps/c4
	cp deps/c4/c4 .

lua: $(shell find src/libc -type f) $(shell find deps/lua -type f)
	$(MAKE) -C deps/lua generic
	cp deps/lua/src/lua .

nanoc: $(shell find src/libc -type f) $(shell find deps/nanoc -type f)
	$(MAKE) -C deps/nanoc
	cp deps/nanoc/nanoc .

doomgeneric: $(shell find src/libc -type f) $(shell find deps/doomgeneric -type f)
	$(MAKE) -C deps/doomgeneric/doomgeneric
	cp deps/doomgeneric/doomgeneric/doomgeneric .

.PHONY: sysroot
sysroot: crt0.o crti.o crtn.o $(LIBS)
	cp -rH src/libc/h/* sysroot/usr/include
	cp -rH src/libui/h/* sysroot/usr/include
	cp libc.a sysroot/usr/lib
	cp libui.a sysroot/usr/lib
	cp libnanoc.a sysroot/usr/lib
	cp crt{0,i,n}.o sysroot/usr/lib

$(APPS): $(shell find src/apps -type f)
	$(MAKE) out=${PWD}/$@ -C src/apps/$@
	cp $@ sysroot/apps

$(BIN): $(shell find src/bin -type f)
	$(MAKE) out=${PWD}/$@ -C src/bin/$@
	cp $@ sysroot/bin

crt0.o: src/libc/crt0.s
	$(AS) $(ASFLAGS) src/libc/crt0.s -o crt0.o

crti.o: src/libc/crti.s
	$(AS) $(ASFLAGS) src/libc/crti.s -o crti.o

crtn.o: src/libc/crtn.s
	$(AS) $(ASFLAGS) src/libc/crtn.s -o crtn.o

$(LIBS): $(shell find src -type f)
	$(MAKE) out=${PWD}/$@ -C src/$(basename $@)

kernel.elf: $(OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS) -o kernel.elf

$(ASM_OBJECTS): $(shell find src -type f)
	$(MAKE) out_asm=${PWD}/$@ -C src/$(basename $(basename $@))

$(OBJECTS): $(shell find src -type f)
	$(MAKE) out=${PWD}/$@ -C src/$(basename $@)

$(DRIVER_OBJECTS): $(shell find src/drivers -type f)
	$(MAKE) out=${PWD}/$@ -C src/drivers/$(basename $@)

mako.iso: kernel.elf tools/make_rd.py $(shell find rdroot -type f)
	python3 tools/make_rd.py
	cp kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o mako.iso iso

.PHONY: qemu
qemu: mako.iso
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso -m 256M \
	                 -drive format=raw,file=hda.img -d cpu_reset

.PHONY: clean
clean:
	rm -rf *.o *.a kernel.elf                                         \
	       iso/boot/kernel.elf mako.iso bochslog.txt com1.out         \
	       iso/modules/rd src/libc/*.o src/libui/*.o src/libnanoc/*.o \
	       sysroot/usr/include/{*,sys/*}.h sysroot/usr/lib/*.{a,o}    \
	       sysroot/bin/* sysroot/apps/* lua c4 doomgeneric nanoc      \
	       $(APPS) $(BIN) hda.img hda.tar ustar_image
