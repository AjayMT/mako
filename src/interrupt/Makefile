
all: $(out) $(out_asm)

$(out): interrupt.c interrupt.h
	$(CC) $(CFLAGS) interrupt.c -o $(out)

$(out_asm): interrupt.s
	$(AS) $(ASFLAGS) interrupt.s -o $(out_asm)
