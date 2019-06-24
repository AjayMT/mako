
// klock.c
//
// Kernel locks.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <interrupt/interrupt.h>
#include <process/process.h>
#include <common/constants.h>
#include <util/util.h>
#include <debug/log.h>
#include "klock.h"

void klock_wait(process_registers_t regs)
{
  if (process_current()) {
    regs.esp += 16;
    u_memcpy(&(process_current()->regs), &regs, sizeof(process_registers_t));
    process_switch_next();
  }
}

void kunlock(klock_t lock)
{ *lock = 0; }
