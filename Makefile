
CC = i386-elf-gcc
CFLAGS = -m32 -nostdlib -fno-builtin -fno-stack-protector \
         -ffreestanding -Wno-unused -Wall -Wextra -Werror \
         -lgcc -I${PWD}/src/ -c
LD = i386-elf-ld
LDFLAGS = -T link.ld -melf_i386
AS = nasm
ASFLAGS = -I${PWD}/src/ -f elf

DRIVER_OBJECTS = io.o framebuffer.o serial.o keyboard.o ata.o \
                 pci.o
ASM_OBJECTS = boot.s.o gdt.s.o idt.s.o interrupt.s.o paging.s.o \
              tss.s.o
OBJECTS = boot.o gdt.o idt.o pic.o interrupt.o paging.o pmm.o \
          debug.o util.o kheap.o fs.o ext2.o ds.o rd.o tss.o
export

all: kernel.elf

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
	qemu-system-i386 -serial file:com1.out -cdrom mako.iso

.PHONY: clean
clean:
	rm -rf *.o kernel.elf                                     \
	       iso/boot/kernel.elf mako.iso bochslog.txt com1.out \
	       iso/modules/rd
