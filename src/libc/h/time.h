
// time.h
//
// Time functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _TIME_H_
#define _TIME_H_

#include <sys/types.h>
#include <stdint.h>

extern char *tzname[2];

struct tm {
  int32_t tm_sec;
  int32_t tm_min;
  int32_t tm_hour;
  int32_t tm_mday;
  int32_t tm_mon;
  int32_t tm_year;
  int32_t tm_wday;
  int32_t tm_yday;
  int32_t tm_isdst;
};

struct tm *localtime(const time_t *timep);
struct tm *gmtime(const time_t *timep);

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
time_t time(time_t *tloc);
double difftime(time_t a, time_t b);
time_t mktime(struct tm *tm);

char *asctime(const struct tm *tm);
char *ctime(const time_t *timep);

typedef uint32_t clock_t;

clock_t clock();

#define CLOCKS_PER_SEC 1

#endif /* _TIME_H_ */
