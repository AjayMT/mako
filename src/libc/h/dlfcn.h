
// dlfcn.h
//
// Dynamic linking.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _DLFCN_H_
#define _DLFCN_H_

void *dlopen(const char *, int);
void *dlsym(void *, const char *);

#endif /* _DLFCN_H_ */
