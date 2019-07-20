
// setjmp.h
//
// Non-local jumping.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SETJMP_H_
#define _SETJMP_H_

#include <stdint.h>

typedef int32_t jmp_buf[9];

void longjmp(jmp_buf j, int r);
int setjmp(jmp_buf j);

#endif /* _SETJMP_H_ */
