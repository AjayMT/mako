
all: $(out_c) $(out_asm)

$(out_c): idt.c idt.h
	$(CC) $(CFLAGS) idt.c -o $(out_c)

$(out_asm): idt.s
	$(AS) $(ASFLAGS) idt.s -o $(out_asm)
