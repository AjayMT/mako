
// syscall.h
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include <process/process.h>
#include <interrupt/interrupt.h>

void interrupt_handler_syscall();

process_registers_t *syscall_handler(cpu_state_t, stack_state_t);

#endif /* _SYSCALL_H_ */
