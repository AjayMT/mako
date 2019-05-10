
all: $(out) $(out_asm)

$(out): gdt.c gdt.h
	$(CC) $(CFLAGS) gdt.c -o $(out)

$(out_asm): gdt.s
	$(AS) $(ASFLAGS) gdt.s -o $(out_asm)
