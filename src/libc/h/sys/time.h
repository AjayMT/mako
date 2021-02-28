
// time.h
//
// More time stuff (gettimeofday, timeval, etc)
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _TIME_H_
#define _TIME_H_

#include <sys/types.h>
#include <stdint.h>

struct timeval {
  time_t tv_sec;
  suseconds_t tv_usec;
};

int32_t gettimeofday(struct timeval *, void *);

#endif /* _TIME_H_ */
