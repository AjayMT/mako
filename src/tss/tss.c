
// tss.c
//
// Task State Segment interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <util/util.h>
#include <common/constants.h>
#include "tss.h"

// Global TSS struct.
static tss_t tss;

// Initialize TSS.
void tss_init()
{
  u_memset(&tss, 0, sizeof(tss_t));
  tss.iopb_offset = sizeof(tss_t);
}

// Get the address of the TSS struct.
uint32_t tss_get_vaddr()
{ return (uint32_t)&tss; }

void tss_set_kernel_stack(uint16_t ss0, uint32_t esp0)
{
  tss.ss0 = ss0;
  tss.esp0 = esp0;
}
