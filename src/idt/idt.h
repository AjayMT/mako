
// idt.h
//
// Interrupt Descriptor Table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _IDT_H_
#define _IDT_H_

// Number of entries in the IDT.
#define IDT_NUM_ENTRIES 256

// Initialize the IDT.
void idt_init();

#endif /* _IDT_H_ */
