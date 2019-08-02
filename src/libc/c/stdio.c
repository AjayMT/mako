
// stdio.c
//
// Standard I/O functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <_syscall.h>
#include <mako.h>
#include <stdio.h>

static FILE _stdin = {
  .fd = 0,
  .eof = 0,
  .offset = 0,
  .ungetcd = EOF
};
static FILE _stdout = {
  .fd = 1,
  .eof = 0,
  .offset = 0,
  .ungetcd = EOF
};
static FILE _stderr = {
  .fd = 2,
  .eof = 0,
  .offset = 0,
  .ungetcd = EOF
};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

void _init_stdio()
{
  stdin->offset = lseek(0, 0, SEEK_CUR);
  if (errno) { errno = 0; stdin->offset = 0; }
  stdout->offset = lseek(1, 0, SEEK_CUR);
  if (errno) { errno = 0; stdout->offset = 0; }
  stderr->offset = lseek(2, 0, SEEK_CUR);
  if (errno) { errno = 0; stderr->offset = 0; }
}

uint32_t parse_mode(const char *mode)
{
  uint32_t flags = 0;
  if (strchr(mode, 'r')) flags |= O_RDONLY;
  if (strchr(mode, 'w')) flags |= O_WRONLY;
  if (strchr(mode, '+')) flags |= O_RDWR;
  return flags;
}

FILE *fopen(const char *path, const char *mode)
{
  uint32_t mode_flags = parse_mode(mode);
  int32_t fd = open(path, mode_flags);
  if (fd == -1) {
    if (errno == ENOENT) {
      fd = open(path, mode_flags | O_CREAT, 0666);
      if (fd == -1) return NULL;
    }
    return NULL;
  }

  FILE *f = malloc(sizeof(FILE));
  f->fd = fd;
  f->eof = 0;
  f->offset = 0;
  f->ungetcd = EOF;
  return f;
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
  FILE *f = fopen(path, mode);
  if (f == NULL) return NULL;

  fclose(stream);
  memcpy(stream, f, sizeof(FILE));
  free(f);
  return stream;
}

int32_t fclose(FILE *f)
{
  fflush(f);
  int32_t r = close(f->fd);
  free(f);
  return r;
}

int32_t fseek(FILE *f, uint64_t offset, int32_t whence)
{
  off_t res = lseek(f->fd, offset, whence);
  if (res == -1) return -1;
  f->offset = res;
  f->eof = 0;
  f->ungetcd = -1;
  return 0;
}
int32_t fseeko(FILE *stream, off_t offset, int32_t whence)
{ return fseek(stream, (uint64_t)offset, whence); }

int64_t ftell(FILE *stream)
{ return stream->offset; }
off_t ftello(FILE *stream)
{ return (off_t)ftell(stream); }

size_t fread(void *ptr, size_t size, size_t nitems, FILE *stream)
{
  size_t total_size = nitems * size;
  if (total_size == 0) return 0;
  char *buf = ptr;
  size_t read_size = 0;
  if (stream->ungetcd != -1) {
    buf[read_size] = stream->ungetcd;
    ++read_size;
    ++(stream->offset);
    stream->ungetcd = -1;
  }

  if (read_size == total_size) return read_size;

  size_t read_size_real = read(
    stream->fd, buf + read_size, total_size - read_size
    );
  stream->offset += read_size_real;
  read_size += read_size_real;
  if (read_size == 0) // Assume it is EOF.
    stream->eof = 1;

  return read_size / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
  size_t total_size = nitems * size;
  if (total_size == 0) return 0;
  size_t written_size = write(stream->fd, ptr, total_size);
  stream->offset += written_size;
  if (written_size == 0) // Assume it is EOF.
    stream->eof = 1;
  return written_size / size;
}

int32_t feof(FILE *stream)
{ return stream->eof; }
int32_t ferror(FILE *stream)
{ return 0; }
int32_t fileno(FILE *stream)
{ return stream->fd; }
int32_t fflush(FILE *stream)
{ return 0; }
void clearerr(FILE *stream)
{ stream->eof = 0; }
void rewind(FILE *stream)
{ clearerr(stream); fseek(stream, 0, SEEK_SET); }

FILE *popen(const char *command, const char *mode)
{
  uint32_t mode_flags = parse_mode(mode);
  if (mode_flags != O_RDONLY || mode_flags != O_WRONLY)
    return NULL;

  // TODO finish this.
  return NULL;
}

int32_t pclose(FILE *stream)
{ return fclose(stream); }

int32_t fputc(int32_t c, FILE *stream)
{
  char cc = (char)c;
  if (fwrite(&cc, 1, 1, stream) == 0)
    return EOF;
  return c;
}
int32_t putc(int32_t c, FILE *stream)
{ return fputc(c, stream); }
int32_t putchar(int32_t c)
{ return putc(c, stdout); }
void _putchar(char c)
{ putchar(c); }

int32_t fgetc(FILE *stream)
{
  char c = EOF;
  fread(&c, 1, 1, stream);
  return (int32_t)c;
}
int32_t getc(FILE *stream)
{ return fgetc(stream); }
int32_t getchar()
{ return getc(stdin); }

char *fgets(char *str, int32_t size, FILE *stream)
{
  for (int32_t i = 0; i < size; ++i) {
    char c = (char)fgetc(stream);
    if (c == EOF) {
      if (i == 0) return NULL;
      return str;
    }
    str[i] = c;
    if (c == '\n') return str;
  }
  return str;
}

int32_t ungetc(int32_t c, FILE *stream)
{
  stream->eof = 0;
  stream->ungetcd = (char)c;
  --(stream->offset);
  return c;
}

int32_t fputs(const char *s, FILE *stream)
{ return fwrite(s, strlen(s), 1, stream); }

void perror(char *s)
{
  if (s && s[0]) fprintf(stderr, "%s: ", s);
  fprintf(stderr, "%s\n", strerror(errno));
}
char *strerror(int32_t errnum)
{
  static char temp[256];
  sprintf(temp, "%d", errnum);
  return temp;
}

int32_t fprintf(FILE *stream, const char *fmt, ...)
{
  static char buffer[512];
  va_list args;
  va_start(args, fmt);
  int32_t size = vsnprintf(buffer, 512, fmt, args);
  va_end(args);
  int32_t out = fwrite(buffer, 1, strlen(buffer), stream);
  return out;
}

FILE *tmpfile()
{ return fopen(tmpnam(NULL), "rw+"); }
char *tmpnam(char *s)
{
  static char temp[L_tmpnam];
  char *buf = s;
  if (s == NULL) buf = temp;
  sprintf(buf, P_tmpdir "%u", getpid());
  return buf;
}

// TODO implement buffering.
int32_t setbuf(FILE *stream, char *buf)
{ return EOF; }
int32_t setvbuf(FILE *stream, char *buf, int32_t type, size_t size)
{ return EOF; }

int32_t remove(const char *path)
{ return unlink(path); }

int32_t rename(const char *old, const char *new)
{
  int32_t res = _syscall2(SYSCALL_RENAME, (uint32_t)old, (uint32_t)new);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
