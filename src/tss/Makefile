
all: $(out) $(out_asm)

$(out): tss.h tss.c
	$(CC) $(CFLAGS) tss.c -o $(out)

$(out_asm): tss.s
	$(AS) $(ASFLAGS) tss.s -o $(out_asm)
