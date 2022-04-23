
// fcntl.h
//
// File control.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _FCNTL_H_
#define _FCNTL_H_

#include "stdint.h"
#include "sys/types.h"

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_APPEND    8
#define O_CREAT     0x200
#define O_TRUNC     0x400
#define O_EXCL      0x800
#define O_NOFOLLOW  0x1000
#define O_PATH      0x2000
#define O_NONBLOCK  0x4000
#define O_DIRECTORY 0x8000

int32_t open(const char *path, int32_t flags, ...);
int32_t chmod(const char *path, mode_t mode);

#endif /* _FCNTL_H_ */
