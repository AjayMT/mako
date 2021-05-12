
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
APPS = dex xed pie img
BIN = init pwd ls read
export

all: kernel.elf

user: deps $(APPS) $(BIN)

deps: sysroot lua c4 doomgeneric
	cp lua sysroot/bin
	cp c4 sysroot/bin
	cp doomgeneric sysroot/apps

c4: $(shell find src/libc -type f) $(shell find deps/c4 -type f)
	$(MAKE) -C deps/c4
	cp deps/c4/c4 .

lua: $(shell find src/libc -type f) $(shell find deps/lua -type f)
	$(MAKE) -C deps/lua generic
	cp deps/lua/src/lua .

doomgeneric: $(shell find src/libc -type f) $(shell find deps/doomgeneric -type f)
	$(MAKE) -C deps/doomgeneric/doomgeneric
	cp deps/doomgeneric/doomgeneric/doomgeneric .

sysroot: crt libc.a libui.a
	cp -rH src/libc/h/* sysroot/usr/include
	cp -rH src/libui/h/* sysroot/usr/include
	cp libc.a sysroot/usr/lib
	cp libui.a sysroot/usr/lib
	cp crt{0,i,n}.o sysroot/usr/lib

$(APPS): $(shell find src/apps -type f)
	$(MAKE) out=${PWD}/$@ -C src/apps/$@
	cp $@ sysroot/apps

$(BIN): $(shell find src/bin -type f)
	$(MAKE) out=${PWD}/$@ -C src/bin/$@
	cp $@ sysroot/bin

crt: crt0.o crti.o crtn.o

crt0.o: src/libc/crt0.s
	$(AS) $(ASFLAGS) src/libc/crt0.s -o crt0.o

crti.o: src/libc/crti.s
	$(AS) $(ASFLAGS) src/libc/crti.s -o crti.o

crtn.o: src/libc/crtn.s
	$(AS) $(ASFLAGS) src/libc/crtn.s -o crtn.o

libc.a: $(shell find src -type f)
	$(MAKE) out=${PWD}/libc.a -C src/libc

libui.a: $(shell find src -type f)
	$(MAKE) out=${PWD}/libui.a -C src/libui

kernel.elf: $(OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS) -o kernel.elf

$(ASM_OBJECTS): $(shell find src -type f)
	$(MAKE) out_asm=${PWD}/$@ -C src/$(basename $(basename $@))

$(OBJECTS): $(shell find src -type f)
	$(MAKE) out=${PWD}/$@ -C src/$(basename $@)

$(DRIVER_OBJECTS): $(shell find src/drivers -type f)
	$(MAKE) out=${PWD}/$@ -C src/drivers/$(basename $@)

rd: make_rd.py $(shell find rdroot -type f)
	python3 make_rd.py

mako.iso: kernel.elf rd
	cp kernel.elf iso/boot/kernel.elf
	i386-elf-grub-mkrescue -o mako.iso iso

bochs: mako.iso
	bochs -f bochsrc.txt -q

qemu: mako.iso
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso -m 256M \
	                 -drive format=raw,file=hda.img -d cpu_reset

.PHONY: clean
clean:
	rm -rf *.o *.a kernel.elf                                      \
	       iso/boot/kernel.elf mako.iso bochslog.txt com1.out      \
	       iso/modules/rd src/libc/*.o src/libui/*.o               \
	       sysroot/usr/include/{*,sys/*}.h sysroot/usr/lib/*.{a,o} \
	       sysroot/bin/* sysroot/apps/* lua c4 doomgeneric $(APPS) $(BIN) hda.img
