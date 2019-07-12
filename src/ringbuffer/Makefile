
all: $(out) $(out_asm)

$(out_asm): ringbuffer.s
	$(AS) $(ASFLAGS) ringbuffer.s -o $(out_asm)

$(out): ringbuffer.c ringbuffer.h
	$(CC) $(CFLAGS) ringbuffer.c -o $(out)
