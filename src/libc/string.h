
// string.h
//
// String functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _STRING_H_
#define _STRING_H_

#include "stdint.h"
#include <stddef.h>

void *memset(void *dest, int32_t c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset32(void *dest, int32_t c, size_t n);
void *memcpy32(void *dest, const void *src, size_t n);
void *memmove(void *dest, void *src, size_t n);

void *memchr(const void *src, int32_t c, size_t n);
int32_t memcmp(const void *p1, const void *p2, size_t n);

char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t len);
char *strchr(const char *s, int32_t c);
char *strrchr(const char *s, int32_t c);
char *strpbrk(const char *s, const char *b);
char *strstr(const char *h, const char *n);

int32_t strcmp(const char *l, const char *r);
int32_t strncmp(const char *s1, const char *s2, size_t n);
int32_t strcoll(const char *s1, const char *s2);

size_t strspn(const char *s, const char *c);
size_t strlen(const char *s);

char *strcat(char *s1, const char *s2);

#endif /*_STRING_H_ */
