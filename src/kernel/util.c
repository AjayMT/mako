
// util.c
//
// Utility functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "util.h"
#include "../common/stdint.h"
#include "constants.h"
#include <stddef.h>

void *u_memset(void *dest, int32_t c, size_t n)
{
  asm volatile("cld; rep stosb" : "=c"((int){ 0 }) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
  return dest;
}

void *u_memset32(void *dest, int32_t c, size_t n)
{
  asm volatile("cld; rep stosl" : "=c"((int){ 0 }) : "D"(dest), "a"(c), "c"(n) : "flags", "memory");
  return dest;
}

void *u_memcpy(void *dest, const void *src, size_t n)
{
  asm volatile("cld; rep movsb"
               : "=c"((int){ 0 })
               : "D"(dest), "S"(src), "c"(n)
               : "flags", "memory");
  return dest;
}

void *u_memcpy32(void *dest, const void *src, size_t n)
{
  asm volatile("cld; rep movsl"
               : "=c"((int){ 0 })
               : "D"(dest), "S"(src), "c"(n)
               : "flags", "memory");
  return dest;
}

size_t u_strlen(const char *s)
{
  size_t i = 0;
  for (; s[i] != '\0'; ++i)
    ;
  return i;
}

int32_t u_strcmp(const char *s1, const char *s2)
{
  for (; *s1 && *s1 == *s2; ++s1, ++s2)
    ;
  return *s1 - *s2;
}

int32_t u_strncmp(const char *s1, const char *s2, size_t n)
{
  uint32_t i = 0;
  for (; *s1 && *s1 == *s2 && i < n; ++s1, ++s2, ++i)
    ;
  if (i == n)
    return 0;
  return *s1 - *s2;
}

size_t u_page_align_up(size_t a)
{
  return u_page_align_down(a + PAGE_SIZE - 1);
}

size_t u_page_align_down(uint32_t a)
{
  return a & 0xFFFFF000;
}
