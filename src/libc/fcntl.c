
// fcntl.c
//
// File control.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "fcntl.h"
#include "_syscall.h"
#include "errno.h"
#include "stdint.h"
#include "sys/types.h"
#include <stdarg.h>

int32_t open(const char *path, int32_t flags, ...)
{
  va_list mode_arg;
  va_start(mode_arg, flags);
  uint32_t mode = 0;
  if (flags & O_CREAT)
    mode = va_arg(mode_arg, uint32_t);
  va_end(mode_arg);
  int32_t res = _syscall3(SYSCALL_OPEN, (uint32_t)path, (uint32_t)flags, mode);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}

int32_t chmod(const char *path, mode_t mode)
{
  int32_t res = _syscall2(SYSCALL_CHMOD, (uint32_t)path, (uint32_t)mode);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}
