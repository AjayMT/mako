
// stdio.h
//
// Standard I/O functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _STDIO_H_
#define _STDIO_H_

#include "stdint.h"
#include <stddef.h>
#include "sys/types.h"

#define BUFSIZ 512

typedef struct {
  uint32_t fd;
  uint8_t eof;
  uint32_t offset;
  char ungetcd;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF -1

#define L_tmpnam 256
#define P_tmpdir "/tmp"

void _init_stdio();

FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *stream);
int32_t fclose(FILE *f);
int32_t fseek(FILE *stream, int64_t offset, int32_t whence);
int32_t fseeko(FILE *stream, off_t offset, int32_t whence);
int64_t ftell(FILE *stream);
off_t ftello(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nitems, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream);
int32_t feof(FILE *stream);
int32_t ferror(FILE *stream);
int32_t fileno(FILE *stream);
int32_t fflush(FILE *stream);
void clearerr(FILE *stream);
void rewind(FILE *stream);

FILE *popen(const char *command, const char *mode);
int32_t pclose(FILE *stream);

#include "_printf.h"

int32_t fprintf(FILE *stream, const char *fmt, ...);

int32_t fputc(int32_t c, FILE *stream);
int32_t putc(int32_t c, FILE *stream);
int32_t putchar(int32_t c);
int32_t fgetc(FILE *stream);
int32_t getc(FILE *stream);
int32_t getchar();
char *fgets(char *str, int32_t size, FILE *stream);
int32_t ungetc(int32_t c, FILE *stream);
int32_t fputs(const char *s, FILE *stream);
int32_t puts(const char *s);

void perror(char *s);
char *strerror(int32_t errnum);

FILE *tmpfile();
char *tmpnam(char *s);

#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

int32_t setbuf(FILE *stream, char *buf);
int32_t setvbuf(FILE *stream, char *buf, int32_t type, size_t size);

int32_t remove(const char *path);
int32_t rename(const char *old, const char *new);

#endif /* _STDIO_H_ */
