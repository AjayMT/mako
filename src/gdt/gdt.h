
// gdt.h
//
// Global descriptor table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _GDT_H_
#define _GDT_H_

#include <stdint.h>

// Constants.
const uint8_t PL0         = 0x0;
const uint8_t PL3         = 0x3;
const uint16_t TSS_SEGSEL = 0x28;

// Initialize the GDT.
void gdt_init(uint32_t);

#endif /* _GDT_H_ */
