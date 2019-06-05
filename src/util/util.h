
// util.h
//
// Utility functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stddef.h>

void *u_memset(void *, char, size_t);
void *u_memcpy(void *, const void *, size_t);
size_t u_strlen(const char *);

#endif /* _UTIL_H_ */
