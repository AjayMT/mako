
// signal.c
//
// Signal handling.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "signal.h"
#include "_syscall.h"
#include "errno.h"
#include "stdint.h"
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"

static sig_t signal_handler_table[NUM_SIGNALS + 1];

static void handle_sig()
{
  uint32_t edx;
  asm volatile("movl %%edx, %0" : "=r"(edx));
  if (edx == SIGKILL || edx == SIGSTOP)
    exit(0);
  if (signal_handler_table[edx])
    signal_handler_table[edx](edx);
  else
    exit(1);
  _syscall0(SYSCALL_SIGNAL_RESUME);
}

void _init_sig()
{
  memset(signal_handler_table, 0, sizeof(signal_handler_table));
  _syscall1(SYSCALL_SIGNAL_REGISTER, (uint32_t)handle_sig);
}

sig_t signal(int32_t num, sig_t handler)
{
  if (num > NUM_SIGNALS || num == SIGKILL || num == SIGSTOP) {
    errno = EINVAL;
    return NULL;
  }

  signal_handler_table[num] = handler;
  return handler;
}

int32_t raise(int32_t num)
{
  if (signal_handler_table[num])
    signal_handler_table[num](num);
  return 0;
}

int32_t signal_send(pid_t pid, int32_t num)
{
  if (num == 0 || num > NUM_SIGNALS) {
    errno = EINVAL;
    return -1;
  }
  int32_t res = _syscall2(SYSCALL_SIGNAL_SEND, pid, (uint32_t)num);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}
