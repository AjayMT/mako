
// stat.c
//
// Filesystem info.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <_syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int32_t stat(const char *file, struct stat *st)
{
  int32_t fd = open(file, O_RDONLY);
  if (fd < 0) { return fd; }
  int32_t res = _syscall2(SYSCALL_FSTAT, (uint32_t)fd, (uint32_t)st);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t lstat(const char *path, struct stat *st)
{
  int32_t res = _syscall2(SYSCALL_LSTAT, (uint32_t)path, (uint32_t)st);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t fstat(const uint32_t fd, struct stat *st)
{
  int32_t res = _syscall2(SYSCALL_FSTAT, fd, (uint32_t)st);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t mkdir(const char *path, int32_t mode)
{
  int32_t res = _syscall2(SYSCALL_MKDIR, (uint32_t)path, (uint32_t)mode);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
