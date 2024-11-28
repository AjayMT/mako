
// time.c
//
// Time functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "time.h"
#include "stdint.h"
#include "string.h"
#include "sys/types.h"
#include <stddef.h>

char *tzname[] = { NULL, NULL };

static struct tm _gtm = { .tm_sec = 0,
                          .tm_min = 0,
                          .tm_hour = 0,
                          .tm_mday = 0,
                          .tm_mon = 0,
                          .tm_year = 0,
                          .tm_wday = 0,
                          .tm_yday = 0,
                          .tm_isdst = 0 };

static struct tm *gtm = &_gtm;

struct tm *localtime(const time_t *timep)
{
  return gtm;
}
struct tm *gmtime(const time_t *timep)
{
  return gtm;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
  static char *stime = "Pizza time!";
  static size_t len = 11;
  if (max > len)
    max = len;
  memcpy(s, stime, max);
  s[max] = '\0';
  return max;
}

time_t time(time_t *tloc)
{
  if (tloc)
    *tloc = 0;
  return 0;
}

double difftime(time_t a, time_t b)
{
  return a - b;
}

time_t mktime(struct tm *tm)
{
  return tm->tm_sec + (60 * tm->tm_min) + (3600 * tm->tm_hour) + (24 * 3600 * tm->tm_yday) +
         (365 * 24 * 3600 * (tm->tm_year - 1970));
}

char *asctime(const struct tm *tm)
{
  static char *stime = "Pizza time!";
  return stime;
}
char *ctime(const time_t *timep)
{
  return asctime(NULL);
}

clock_t clock()
{
  return 0x420;
}
