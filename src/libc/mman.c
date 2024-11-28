
// mman.c
//
// mmap stub
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "sys/mman.h"
#include "sys/types.h"
#include <stddef.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
  return NULL;
}
