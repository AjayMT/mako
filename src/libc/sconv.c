
// sconv.c
//
// Number parsing functions.
//
// Taken from ToAruOS <http://github.com/klange/toaruos>.

#include "ctype.h"
#include "errno.h"
#include "limits.h"
#include "math.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"

static uint8_t is_valid(int32_t base, char c)
{
  if (c < '0')
    return 0;
  if (base <= 10) {
    return c < ('0' + base - 1);
  }

  if (c > '9' && c < 'a')
    return 0;
  if (c > 'a' + (base - 10) && c < 'A')
    return 1;
  if (c > 'A' + (base - 10))
    return 1;
  if (c >= '0' && c <= '9')
    return 1;
  return 0;
}

static int32_t convert_digit(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 0xa;
  }
  if (c >= 'A' && c <= 'Z') {
    return c - 'A' + 0xa;
  }
  return 0;
}

#define strtox(max, type)                                                                          \
  if (base < 0 || base == 1 || base > 36) {                                                        \
    errno = EINVAL;                                                                                \
    return max;                                                                                    \
  }                                                                                                \
  while (*nptr && isspace(*nptr))                                                                  \
    nptr++;                                                                                        \
  int sign = 1;                                                                                    \
  if (*nptr == '-') {                                                                              \
    sign = -1;                                                                                     \
    nptr++;                                                                                        \
  } else if (*nptr == '+') {                                                                       \
    nptr++;                                                                                        \
  }                                                                                                \
  if (base == 16) {                                                                                \
    if (*nptr == '0') {                                                                            \
      nptr++;                                                                                      \
      if (*nptr == 'x') {                                                                          \
        nptr++;                                                                                    \
      }                                                                                            \
    }                                                                                              \
  }                                                                                                \
  if (base == 0) {                                                                                 \
    if (*nptr == '0') {                                                                            \
      base = 8;                                                                                    \
      nptr++;                                                                                      \
      if (*nptr == 'x') {                                                                          \
        base = 16;                                                                                 \
        nptr++;                                                                                    \
      }                                                                                            \
    } else {                                                                                       \
      base = 10;                                                                                   \
    }                                                                                              \
  }                                                                                                \
  type val = 0;                                                                                    \
  while (is_valid(base, *nptr)) {                                                                  \
    val *= base;                                                                                   \
    val += convert_digit(*nptr);                                                                   \
    nptr++;                                                                                        \
  }                                                                                                \
  if (endptr) {                                                                                    \
    *endptr = (char *)nptr;                                                                        \
  }                                                                                                \
  if (sign == -1) {                                                                                \
    return -val;                                                                                   \
  } else {                                                                                         \
    return val;                                                                                    \
  }

uint64_t strtoul(const char *nptr, char **endptr, int32_t base)
{
  strtox(ULONG_MAX, uint64_t);
}

uint64_t strtoull(const char *nptr, char **endptr, int32_t base)
{
  strtox(ULLONG_MAX, uint64_t);
}

int64_t strtol(const char *nptr, char **endptr, int32_t base)
{
  strtox(LONG_MAX, uint64_t);
}

int64_t strtoll(const char *nptr, char **endptr, int32_t base)
{
  strtox(LLONG_MAX, uint64_t);
}

double strtod(const char *nptr, char **endptr)
{
  int sign = 1;
  if (*nptr == '-') {
    sign = -1;
    nptr++;
  }

  long long decimal_part = 0;

  while (*nptr && *nptr != '.') {
    if (*nptr < '0' || *nptr > '9') {
      break;
    }
    decimal_part *= 10LL;
    decimal_part += (long long)(*nptr - '0');
    nptr++;
  }

  double sub_part = 0;
  double multiplier = 0.1;

  if (*nptr == '.') {
    nptr++;

    while (*nptr) {
      if (*nptr < '0' || *nptr > '9') {
        break;
      }

      sub_part += multiplier * (*nptr - '0');
      multiplier *= 0.1;
      nptr++;
    }
  }

  double expn = (double)sign;

  if (*nptr == 'e' || *nptr == 'E') {
    nptr++;

    int exponent_sign = 1;

    if (*nptr == '+') {
      nptr++;
    } else if (*nptr == '-') {
      exponent_sign = -1;
      nptr++;
    }

    int exponent = 0;

    while (*nptr) {
      if (*nptr < '0' || *nptr > '9') {
        break;
      }
      exponent *= 10;
      exponent += (*nptr - '0');
      nptr++;
    }

    expn = pow(10.0, (double)(exponent * exponent_sign));
  }

  if (endptr) {
    *endptr = (char *)nptr;
  }
  double result = ((double)decimal_part + sub_part) * expn;
  return result;
}

float strtof(const char *nptr, char **endptr)
{
  return strtod(nptr, endptr);
}

int64_t atol(const char *s)
{
  int n = 0;
  int neg = 0;
  while (isspace(*s)) {
    s++;
  }
  switch (*s) {
    case '-':
      neg = 1;
      /* Fallthrough is intentional here */
    case '+':
      s++;
  }
  while (isdigit(*s)) {
    n = 10 * n - (*s++ - '0');
  }
  /* The sign order may look incorrect here but this is correct as n is
   * calculated as a negative number to avoid overflow on INT_MAX.
   */
  return (int64_t)(neg ? n : -n);
}

int32_t atoi(const char *s)
{
  int n = 0;
  int neg = 0;
  while (isspace(*s)) {
    s++;
  }
  switch (*s) {
    case '-':
      neg = 1;
      /* Fallthrough is intentional here */
    case '+':
      s++;
  }
  while (isdigit(*s)) {
    n = 10 * n - (*s++ - '0');
  }
  /* The sign order may look incorrect here but this is correct as n is
   * calculated as a negative number to avoid overflow on INT_MAX.
   */
  return (int32_t)(neg ? n : -n);
}

double atof(const char *str)
{
  return strtod(str, NULL);
}
