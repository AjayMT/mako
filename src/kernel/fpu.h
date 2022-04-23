
// fpu.h
//
// FPU context handling.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _FPU_H_
#define _FPU_H_

#include "process.h"

void fpu_init();
void fpu_save(process_t *);
void fpu_restore(process_t *);

#endif /* _FPU_H_ */
