
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
