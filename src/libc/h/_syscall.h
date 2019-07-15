
// _syscall.h
//
// Syscall interrupt interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef __SYSCALL_H_
#define __SYSCALL_H_

#include <stdint.h>
#include <syscall/syscall_nums.h>

int32_t _syscall0(uint32_t);
int32_t _syscall1(uint32_t, uint32_t);
int32_t _syscall2(uint32_t, uint32_t, uint32_t);
int32_t _syscall3(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t _syscall4(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif /* __SYSCALL_H_ */
