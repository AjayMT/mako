
// kheap.h
//
// Kernel heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _KHEAP_H_
#define _KHEAP_H_

#include <stddef.h>

// Allocate memory.
void *kmalloc(size_t);

// Free memory.
void kfree(void *);

#endif /* _KHEAP_H_ */
