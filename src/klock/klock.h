
// klock.h
//
// Kernel locks.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _KLOCK_H_
#define _KLOCK_H_

#include <stdint.h>

typedef volatile uint32_t *klock_t;

void klock(klock_t);
void kunlock(klock_t);

#endif /* _KLOCK_H_ */
