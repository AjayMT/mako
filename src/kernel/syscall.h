
// syscall.h
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include "../common/stdint.h"
#include "interrupt.h"
#include "process.h"

void interrupt_handler_syscall();

process_registers_t *syscall_handler(cpu_state_t, stack_state_t);

int32_t syscall0(uint32_t);
int32_t syscall1(uint32_t, uint32_t);
int32_t syscall2(uint32_t, uint32_t, uint32_t);
int32_t syscall3(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t syscall4(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#endif /* _SYSCALL_H_ */
