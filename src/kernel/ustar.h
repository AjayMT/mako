
// ustar.h
//
// USTAR filesystem implementation.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _USTAR_H_
#define _USTAR_H_

#include "../common/stdint.h"

#define USTAR_ROOT "/"

uint32_t ustar_init(const char *);

#endif /* _USTAR_H_ */
