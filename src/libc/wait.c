
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
  struct _wait_result *st = (struct _wait_result *)stat_loc;
  int32_t res = _syscall2(SYSCALL_WAIT, pid, (uint32_t)st);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
