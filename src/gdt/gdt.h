
// gdt.h
//
// Global descriptor table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _GDT_H_
#define _GDT_H_

#include <common/stdint.h>

// Privilege level constants.
static const uint8_t PL0            = 0x0;
static const uint8_t PL3            = 0x3;

// Initialize the GDT.
void gdt_init();

#endif /* _GDT_H_ */
