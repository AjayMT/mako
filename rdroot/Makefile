
CC = i686-pc-mako-gcc
CFLAGS = -c
LD = i686-pc-mako-gcc

all: test test2

test: test.o
	$(LD) test.o -o test -lui

test2: test2.o
	$(LD) test2.o -o test2 -lui

test.o: test.c
	$(CC) $(CFLAGS) test.c -o test.o

test2.o: test2.c
	$(CC) $(CFLAGS) test2.c -o test2.o

.PHONY: clean
clean:
	rm -fr test test2 *.o
