
// signal.h
//
// Signal handling.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SIGNAL_H_
#define _SIGNAL_H_

#include <stdint.h>
#include <sys/types.h>
#include <_signal.h>

#define SIG_DFL NULL

typedef void (*sig_t)(int);
typedef int32_t sig_atomic_t;

void _init_sig();
sig_t signal(int32_t num, sig_t handler);
int32_t raise(int32_t num);
int32_t signal_send(pid_t pid, int32_t num);

#endif /* _SIGNAL_H_ */
