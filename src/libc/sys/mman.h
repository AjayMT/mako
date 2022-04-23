
// mman.h
//
// mmap stub
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _MMAN_H_
#define _MMAN_H_

#include <stddef.h>
#include "types.h"

void *mmap(void *, size_t, int, int, int, off_t);

#endif /* _MMAN_H_ */
