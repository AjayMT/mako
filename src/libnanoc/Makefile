
AR = i386-elf-ar
ARFLAGS = rcs

all: $(out)

$(out): nanoc.o
	$(AR) $(ARFLAGS) $(out) nanoc.o

nanoc.o: nanoc.c
	$(CC) -I../libc/h $(CFLAGS) nanoc.c -o nanoc.o

.PHONY: clean
clean:
	rm -fr *.o
