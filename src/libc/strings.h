
// strings.h
//
// String functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _STRINGS_H_
#define _STRINGS_H_

#include "stdint.h"
#include <stddef.h>

int32_t strcasecmp(const char *s1, const char *s2);
int32_t strncasecmp(const char *s1, const char *s2, size_t n);

#endif /* _STRINGS_H_ */
