
// util.c
//
// Utility functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "util.h"

void *u_memset(void *b, char c, size_t len)
{
  for (size_t i = 0; i < len; ++i) ((char *)b)[i] = c;
  return b;
}

void *u_memcpy(void *dst, const void *src, size_t n)
{
  for (size_t i = 0; i < n; ++i)
    ((char *)dst)[i] = ((char *)src)[i];
  return dst;
}

size_t u_strlen(const char *s)
{
  size_t i = 0;
  for (; s[i] != '\0'; ++i);
  return i;
}
