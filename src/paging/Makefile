
all: $(out) $(out_asm)

$(out): paging.c paging.h
	$(CC) $(CFLAGS) paging.c -o $(out)

$(out_asm): paging.s
	$(AS) $(ASFLAGS) paging.s -o $(out_asm)
