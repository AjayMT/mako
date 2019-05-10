
all: $(out) $(out_asm)

$(out): idt.c idt.h
	$(CC) $(CFLAGS) idt.c -o $(out)

$(out_asm): idt.s
	$(AS) $(ASFLAGS) idt.s -o $(out_asm)
