
all: $(out) $(out_asm)

$(out): syscall.c syscall.h
	$(CC) $(CFLAGS) syscall.c -o $(out)

$(out_asm): syscall.s
	$(AS) $(ASFLAGS) syscall.s -o $(out_asm)
