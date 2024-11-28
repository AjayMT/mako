
// klock.c
//
// Kernel locks.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "klock.h"
#include "../common/stdint.h"
#include "constants.h"
#include "interrupt.h"
#include "log.h"
#include "process.h"
#include "util.h"

void klock_wait(process_registers_t regs)
{
  if (process_current()) {
    regs.esp += 16;
    u_memcpy(&(process_current()->kregs), &regs, sizeof(process_registers_t));
    process_switch_next();
  }
}

void kunlock(klock_t lock)
{
  *lock = 0;
}
