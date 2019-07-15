
AR = i386-elf-ar
ARFLAGS = rcs

LIBC_OBJECTS = _syscall.o

all: $(out)

$(out): $(LIBC_OBJECTS)
	$(AR) $(ARFLAGS) $(out) $(LIBC_OBJECTS)

%.o: c/%.c
	$(CC) -Ih/ $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -fr *.o
