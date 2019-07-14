
// fpu.c
//
// FPU context handling.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <process/process.h>
#include <util/util.h>
#include "fpu.h"

static uint8_t saved_fp_regs[512] __attribute__((aligned(16)));

void fpu_save(process_t *p)
{
  asm volatile ("fxsave (%0)" :: "r"(saved_fp_regs));
  u_memcpy(p->fpregs, saved_fp_regs, 512);
}

void fpu_restore(process_t *p)
{
  u_memcpy(saved_fp_regs, p->fpregs, 512);
  asm volatile ("fxrstor (%0)" :: "r"(saved_fp_regs));
}
