
// gdt.h
//
// Global descriptor table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _GDT_H_
#define _GDT_H_

#include <stdint.h>

// Constants.
#define PL0        0x0
#define PL3        0x3
#define TSS_SEGSEL 0x28

// Initialize the GDT.
void gdt_init(uint32_t);

#endif /* _GDT_H_ */
