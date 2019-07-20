
// _syscall.h
//
// Syscall interrupt interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef __SYSCALL_H_
#define __SYSCALL_H_

#include <stdint.h>
#include <_syscall_nums.h>

int32_t _syscall0(const uint32_t);
int32_t _syscall1(const uint32_t, const uint32_t);
int32_t _syscall2(const uint32_t, const uint32_t, const uint32_t);
int32_t _syscall3(
  const uint32_t, const uint32_t, const uint32_t, const uint32_t
  );
int32_t _syscall4(
  const uint32_t,
  const uint32_t,
  const uint32_t,
  const uint32_t,
  const uint32_t
  );

#endif /* __SYSCALL_H_ */
