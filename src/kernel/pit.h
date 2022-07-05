
// pit.h
//
// Programmable Interval Timer interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PIT_H_
#define _PIT_H_

#include "../common/stdint.h"
#include "interrupt.h"

// Initialize the PIT.
void pit_init();

// Get and set interval (in milliseconds).
uint32_t pit_get_interval();
void pit_set_interval(uint32_t);

// Get time.
uint64_t pit_get_time();

// Set interrupt handler.
void pit_set_handler(interrupt_handler_t);

#endif /* _PIT_H_ */
