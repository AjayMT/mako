
CC = i386-elf-gcc
CFLAGS = -g -nostdlib -fstack-protector-explicit \
         -ffreestanding -Wno-unused -Wall -Wextra -Werror \
         -Wno-implicit-fallthrough -lgcc -I${PWD}/src/ -c
LD = i386-elf-ld
LDFLAGS = -T link.ld -melf_i386
AS = nasm
ASFLAGS = -g -I${PWD}/src/ -f elf

DRIVER_OBJECTS = io.o framebuffer.o serial.o keyboard.o ata.o \
                 pci.o
ASM_OBJECTS = boot.s.o gdt.s.o idt.s.o interrupt.s.o paging.s.o \
              tss.s.o process.s.o syscall.s.o klock.s.o         \
              ringbuffer.s.o fpu.s.o
OBJECTS = boot.o gdt.o idt.o pic.o interrupt.o paging.o pmm.o  \
          debug.o util.o kheap.o fs.o ext2.o ds.o rd.o tss.o   \
          process.o pit.o elf.o syscall.o klock.o ringbuffer.o \
          pipe.o fpu.o rtc.o
export

all: kernel.elf

crt: crt0.o crti.o crtn.o

crt0.o: src/libc/crt0.s
	$(AS) $(ASFLAGS) src/libc/crt0.s -o crt0.o

crti.o: src/libc/crti.s
	$(AS) $(ASFLAGS) src/libc/crti.s -o crti.o

crtn.o: src/libc/crtn.s
	$(AS) $(ASFLAGS) src/libc/crtn.s -o crtn.o

libc.a: $(shell find src -type f)
	$(MAKE) out=${PWD}/libc.a -C src/libc

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
	mkisofs -R                              \
	        -b boot/grub/stage2_eltorito    \
	        -no-emul-boot                   \
	        -boot-load-size 4               \
	        -A os                           \
	        -input-charset utf8             \
	        -quiet                          \
	        -boot-info-table                \
	        -o mako.iso                     \
	        iso

bochs: mako.iso
	bochs -f bochsrc.txt -q

qemu: mako.iso
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso \
	                 -drive format=raw,file=hda.img -d cpu_reset

.PHONY: clean
clean:
	rm -rf *.o *.a kernel.elf                                 \
	       iso/boot/kernel.elf mako.iso bochslog.txt com1.out \
	       iso/modules/rd src/libc/*.o
