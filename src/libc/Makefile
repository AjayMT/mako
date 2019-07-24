
AR = i386-elf-ar
ARFLAGS = rcs

LIBC_OBJECTS = _syscall.o heap.o dirent.o fcntl.o errno.o stdio.o  \
               printf.o stdlib.o string.o unistd.o ctype.o math.o  \
               sconv.o libgen.o libintl.o locale.o mako.o signal.o \
               stat.o time.o utime.o wait.o setjmp.o

all: $(out)

$(out): $(LIBC_OBJECTS)
	$(AR) $(ARFLAGS) $(out) $(LIBC_OBJECTS)

setjmp.o: c/setjmp.s
	$(AS) $(ASFLAGS) c/setjmp.s -o setjmp.o

%.o: c/%.c
	$(CC) -Ih/ $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -fr *.o
