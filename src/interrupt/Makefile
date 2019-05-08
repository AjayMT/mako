
all: $(out_c) $(out_asm)

$(out_c): interrupt.c interrupt.h
	$(CC) $(CFLAGS) interrupt.c -o $(out_c)

$(out_asm): interrupt.s
	$(AS) $(ASFLAGS) interrupt.s -o $(out_asm)
