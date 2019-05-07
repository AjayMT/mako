
all: $(out_c) $(out_asm)

$(out_c): gdt.c gdt.h
	$(CC) $(CFLAGS) gdt.c -o $(out_c)

$(out_asm): gdt.s
	$(AS) $(ASFLAGS) gdt.s -o $(out_asm)
