
AR = i386-elf-ar
ARFLAGS = rcs

all: $(out)

$(out): ui.o
	$(AR) $(ARFLAGS) $(out) ui.o

ui.o: c/ui.c h/ui.h
	$(CC) -I../libc/h -Ih/ $(CFLAGS) c/ui.c -o ui.o

.PHONY: clean
clean:
	rm -fr *.o
