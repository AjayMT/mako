
// wait.c
//
// Process wait functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "stdint.h"
#include "sys/types.h"
#include "_syscall.h"
#include "errno.h"
#include "sys/wait.h"

int32_t waitpid(pid_t pid, int32_t *stat_loc, int32_t options)
{
  int32_t res = _syscall1(SYSCALL_WAIT, pid);
  *stat_loc = res;
  if (res < 0) { errno = -res; return -1; }
  return pid;
}
