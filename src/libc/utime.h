
// utime.h
//
// Set file atime/mtime/ctime.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UTIME_H_
#define _UTIME_H_

#include "stdint.h"
#include "sys/types.h"

struct utimbuf
{
  time_t actime;
  time_t modtime;
};

int32_t utime(const char *path, const struct utimbuf *times);

#endif /* _UTIME_H_ */
