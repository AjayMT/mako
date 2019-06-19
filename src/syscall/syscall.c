
// syscall.c
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <process/process.h>
#include <interrupt/interrupt.h>
#include <fs/fs.h>
#include <debug/log.h>
#include "syscall.h"

void syscall_fork()
{
  process_t *current = process_current();
  process_t *child = process_fork(current);
  child->regs.eax = 0;
  current->regs.eax = child->pid;
  process_schedule(child);
}

void syscall_execve(char *path, char *argv[], char *envp[])
{
  // TODO.
}

void syscall_exit(uint32_t status)
{
  process_finish(process_current());
  process_switch_next();
}

process_registers_t *syscall_handler(cpu_state_t cs, stack_state_t ss)
{
  update_current_process_registers(cs, ss);
  return &(process_current()->regs);
}
