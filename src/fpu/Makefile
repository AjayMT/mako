
all: $(out) $(out_asm)

$(out): fpu.h fpu.c
	$(CC) $(CFLAGS) fpu.c -o $(out)

$(out_asm): fpu.s
	$(AS) $(ASFLAGS) fpu.s -o $(out_asm)
