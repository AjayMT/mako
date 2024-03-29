
// util.h
//
// Utility functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UTIL_H_
#define _UTIL_H_

#include <stddef.h>
#include "../common/stdint.h"

void *u_memset(void *, int32_t, size_t);
void *u_memcpy(void *, const void *, size_t);
size_t u_strlen(const char *);
int32_t u_strcmp(const char *, const char *);
int32_t u_strncmp(const char *, const char *, size_t);
size_t u_page_align_up(size_t a);
size_t u_page_align_down(uint32_t a);

#endif /* _UTIL_H_ */
