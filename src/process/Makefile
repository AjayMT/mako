
all: $(out) $(out_asm)

$(out): process.h process.c
	$(CC) $(CFLAGS) process.c -o $(out)

$(out_asm): process.s
	$(AS) $(ASFLAGS) process.s -o $(out_asm)
