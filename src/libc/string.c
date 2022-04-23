
// string.c
//
// String functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "stdint.h"
#include <stddef.h>
#include "stdlib.h"
#include "errno.h"
#include "string.h"

void *memcpy(void *dest, const void *src, size_t n)
{
  asm volatile (
    "cld; rep movsb"
    : "=c"((int){0})
    : "D"(dest), "S"(src), "c"(n)
    : "flags", "memory"
    );
  return dest;
}

void *memset(void *dest, int32_t c, size_t n)
{
  asm volatile(
    "cld; rep stosb"
    : "=c"((int){0})
    : "D"(dest), "a"(c), "c"(n)
    : "flags", "memory"
    );
  return dest;
}

void *memmove(void *dest, void *src, size_t n)
{
  char *d = dest;
  char *s = src;
  if (d == s) return d;
  if (d < s) {
    for (; n; --n, ++d, ++s) *d = *s;
    return d;
  }
  for (int32_t i = n - 1; i >= 0; --i) d[i] = s[i];
  return d;
}

void *memchr(const void *src, int32_t c, size_t n)
{
  const uint8_t *s = src;
  c = (uint8_t)c;
  for (uint32_t i = 0; i < n; ++i)
    if (s[i] == c) return (uint8_t *)(s + i);
  return NULL;
}

int32_t memcmp(const void *p1, const void *p2, size_t n)
{
  const char *s1 = p1;
  const char *s2 = p2;
  uint32_t i = 0;
  for (; i < n && s1[i] == s2[i]; ++i);
  return i == n ? 0 : s1[i] - s2[i];
}

char *strdup(const char *s)
{
  size_t n = strlen(s);
  char *new = malloc(n + 1);
  if (new == NULL) { errno = ENOMEM; return NULL; }
  memcpy(new, s, n + 1);
  return new;
}

char *strndup(const char *s, size_t n)
{
  char *new = malloc(n + 1);
  if (new == NULL) { errno = ENOMEM; return NULL; }
  strncpy(new, s, n);
  new[n] = 0;
  return new;
}

char *strcpy(char *dest, const char *src)
{ return memcpy(dest, src, strlen(src) + 1); }
char *strncpy(char *dest, const char *src, size_t len)
{
  size_t slen = strlen(src);
  size_t min = slen < len ? slen : len;
  memcpy(dest, src, min);
  if (slen < len) memset(dest + slen, 0, len - slen);
  return dest;
}

char *strchr(const char *s, int32_t c)
{
  c = (char)c;
  uint32_t n = strlen(s);
  for (uint32_t i = 0; i < n + 1; ++i)
    if (s[i] == c) return (char *)(s + i);
  return NULL;
}

char *strrchr(const char *s, int32_t c)
{
  c = (char)c;
  uint32_t n = strlen(s);
  for (int32_t i = n; i >= 0; --i)
    if (s[i] == c) return (char *)(s + i);
  return NULL;
}

char *strpbrk(const char *s, const char *charset)
{
  uint32_t n = strlen(s);
  uint32_t m = strlen(charset);
  for (uint32_t i = 0; i < n; ++i)
    for (uint32_t j = 0; j < m; ++j)
      if (s[i] == charset[j]) return (char *)(s + i);
  return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
  uint32_t n = strlen(haystack);
  uint32_t m = strlen(needle);
  if (m > n) return NULL;
  if (m == 0) return (char *)haystack;
  for (uint32_t i = 0; i < n; ++i) {
    if (haystack[i] != needle[0]) continue;
    if (strncmp(haystack + i, needle, m) == 0)
      return (char *)(haystack + i);
  }
  return NULL;
}

int32_t strcmp(const char *s1, const char *s2)
{
  for (; *s1 && *s1 == *s2; ++s1, ++s2);
  return *s1 - *s2;
}

int32_t strncmp(const char *s1, const char *s2, size_t n)
{
  uint32_t i = 0;
  for (; *s1 && *s1 == *s2 && i < n; ++s1, ++s2, ++i);
  if (i == n) return 0;
  return *s1 - *s2;
}

int32_t strcoll(const char *s1, const char *s2)
{ return strcmp(s1, s2); }

size_t strspn(const char *s, const char *charset)
{
  uint32_t n = strlen(s);
  uint32_t m = strlen(charset);
  size_t i = 0;
  for (; i < n; ++i) {
    uint32_t count = 0;
    for (; count < m && charset[count] != s[i]; ++count);
    if (count == m) break;
  }
  return i;
}

size_t strlen(const char *s)
{
  size_t i = 0;
  for (; s[i]; ++i);
  return i;
}

char *strcat(char *s1, const char *s2)
{
  size_t s1len = strlen(s1);
  size_t s2len = strlen(s2);
  for (size_t i = 0; i < s2len; ++i)
    s1[s1len + i] = s2[i];
  s1[s1len + s2len] = '\0';
  return s1;
}
