
# makefiles are disgusting

CC = i386-elf-gcc
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -nostartfiles -nodefaultlibs -Wno-unused-function -Wall -Wextra -Werror -I${PWD}/src/ -c
LD = i386-elf-ld
LDFLAGS = -T link.ld -melf_i386
AS = nasm
ASFLAGS = -f elf

DRIVER_OBJECTS = io.o framebuffer.o serial.o
ASM_OBJECTS = gdt.s.o
C_OBJECTS = gdt.o
export

all: kernel.elf

kernel.elf: boot.o kmain.o $(C_OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS)
	$(LD) $(LDFLAGS) boot.o kmain.o $(C_OBJECTS) $(ASM_OBJECTS) $(DRIVER_OBJECTS) -o kernel.elf

boot.o: src/kernel/boot.s
	$(AS) $(ASFLAGS) src/kernel/boot.s -o boot.o

kmain.o: src/kernel/kmain.c
	$(CC) $(CFLAGS) src/kernel/kmain.c -o kmain.o

$(ASM_OBJECTS): $(shell find src -type f)
	$(MAKE) out_asm=${PWD}/$@ -C src/$(basename $(basename $@))

$(C_OBJECTS): $(shell find src -type f)
	$(MAKE) out_c=${PWD}/$@ -C src/$(basename $@)

$(DRIVER_OBJECTS): $(shell find src/drivers -type f)
	$(MAKE) out=${PWD}/$@ -C src/drivers/$(basename $@)

mako.iso: kernel.elf
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

run: mako.iso
	bochs -f bochsrc.txt -q

clean:
	rm -rf *.o kernel.elf iso/boot/kernel.elf mako.iso bochslog.txt
