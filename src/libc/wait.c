
// wait.c
//
// Process wait functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "sys/wait.h"
#include "_syscall.h"
#include "errno.h"
#include "stdint.h"
#include "sys/types.h"

int32_t waitpid(pid_t pid, int32_t *stat_loc, int32_t options)
{
  int32_t res = _syscall1(SYSCALL_WAIT, pid);
  *stat_loc = res;
  if (res < 0) {
    errno = -res;
    return -1;
  }
  return pid;
}
