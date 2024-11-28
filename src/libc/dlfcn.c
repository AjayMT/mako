
// dlfcn.c
//
// Dynamic linking.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "dlfcn.h"
#include <stddef.h>

// These are stubs for now.

void *dlopen(const char *path, int mode)
{
  return NULL;
}

void *dlsym(void *handle, const char *symbol)
{
  return NULL;
}
