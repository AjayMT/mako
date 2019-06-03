
// io.h
//
// Serial port I/O interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _IO_H_
#define _IO_H_

#include <stdint.h>

// Output functions. Defined in io.s.
void outb(uint16_t port, uint8_t data);
void outw(uint16_t port, uint16_t data);
void outl(uint16_t port, uint32_t data);

// Input functions. Defined in io.s.
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);

#endif /* _IO_H_ */
