
// systime.c
//
// More time stuff (gettimeofday, timeval, etc)
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "mako.h"
#include "stdint.h"
#include "sys/time.h"
#include "sys/types.h"

int32_t gettimeofday(struct timeval *tv, void *tz)
{
  uint32_t t = systime();
  if (tv == NULL)
    return 0;
  tv->tv_sec = t / 1000;
  tv->tv_usec = t * 1000;
  return 0;
}
