
all: $(out) $(out_asm)

$(out): kmain.c
	$(CC) $(CFLAGS) kmain.c -o $(out)

$(out_asm): boot.s
	$(AS) $(ASFLAGS) boot.s -o $(out_asm)
