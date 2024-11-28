
// ctype.c
//
// Characters.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ctype.h"
#include "stdint.h"

int32_t isalnum(int32_t c)
{
  return isalpha(c) || isdigit(c);
}
int32_t isalpha(int32_t c)
{
  return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}
int32_t isascii(int32_t c)
{
  return (c <= 0x7f);
}
int32_t iscntrl(int32_t c)
{
  return ((c >= 0 && c <= 0x1f) || (c == 0x7f));
}
int32_t isdigit(int32_t c)
{
  return (c >= '0' && c <= '9');
}
int32_t isgraph(int32_t c)
{
  return (c >= '!' && c <= '~');
}
int32_t islower(int32_t c)
{
  return (c >= 'a' && c <= 'z');
}
int32_t isprint(int32_t c)
{
  return isgraph(c) || c == ' ';
}
int32_t ispunct(int32_t c)
{
  return isgraph(c) && !isalnum(c);
}
int32_t isspace(int32_t c)
{
  return (c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == ' ');
}
int32_t isupper(int32_t c)
{
  return (c >= 'A' && c <= 'Z');
}
int32_t isxdigit(int32_t c)
{
  return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
}
int32_t tolower(int32_t c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 'a';
  return c;
}
int32_t toupper(int32_t c)
{
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 'A';
  return c;
}
