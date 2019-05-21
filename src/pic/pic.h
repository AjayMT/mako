
// pic.h
//
// Programmable Interrupt Controller interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PIC_H_
#define _PIC_H_

#include <stdint.h>

// Initialize the PICs.
void pic_init();

// Send acknowledgement back to the PICs.
void pic_acknowledge(uint8_t);

// Set PIC masks. Each bit is an index of an IRQ, masked IRQs
// are ignpred.
void pic_mask(uint8_t, uint8_t);

#endif /* _PIC_H_ */
