
// strings.h
//
// String functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "strings.h"
#include "ctype.h"
#include "stdint.h"

int32_t strcasecmp(const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  int32_t result;
  if (p1 == p2)
    return 0;
  while ((result = tolower(*p1) - tolower(*p2++)) == 0)
    if (*p1++ == '\0')
      break;
  return result;
}

int32_t strncasecmp(const char *s1, const char *s2, size_t n)
{
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  int result;
  if (p1 == p2 || n == 0)
    return 0;
  while ((result = tolower(*p1) - tolower(*p2++)) == 0)
    if (*p1++ == '\0' || --n == 0)
      break;
  return result;
}
