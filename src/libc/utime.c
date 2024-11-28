
// utime.c
//
// Set file atime/mtime/ctime.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "utime.h"
#include "errno.h"
#include "stdint.h"
#include "sys/types.h"

int32_t utime(const char *path, const struct utimbuf *times)
{
  errno = EIO;
  return -1;
}
