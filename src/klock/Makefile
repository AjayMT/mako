
$(out_asm): klock.s
	$(AS) $(ASFLAGS) klock.s -o $(out_asm)

$(out): klock.c klock.h
	$(CC) $(CFLAGS) klock.c -o $(out)
