
// stdlib.h
//
// Standard library functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <stdint.h>
#include <stddef.h>

extern char **environ;

char *getenv(const char *name);
int32_t setenv(const char *name, const char *value, int32_t overwrite);
int32_t unsetenv(const char *name);

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int32_t atexit(void *);
void exit(int32_t status);
void abort();
int32_t system(const char *command);

int32_t abs(int32_t i);
int64_t labs(int64_t l);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t n, size_t s);
void *realloc(void *ptr, size_t size);

double strtod(const char *str, char **endptr);
float strtof(const char *str, char **endptr);
double atof(const char *str);
int32_t atoi(const char *str);
int64_t atol(const char *str);
int64_t strtol(const char *s, char **endptr, int32_t base);
int64_t strtoll(const char *s, char **endptr, int32_t base);
uint64_t strtoul(const char *str, char **endptr, int32_t base);
uint64_t strtoull(const char *str, char **endptr, int32_t base);

#define RAND_MAX 0x7FFFFFFF

int32_t rand();
void srand(uint32_t seed);

void qsort(
  void *base,
  size_t n,
  size_t width,
  int (*cmp)(const void *, const void *)
  );

#endif /* _STDLIB_H_ */
