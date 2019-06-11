
// pit.h
//
// Programmable Interval Timer interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PIT_H_
#define _PIT_H_

#include <stdint.h>

// Initialize the PIT.
void pit_init();

// Set interval.
void pit_set_interval(uint32_t);

#endif /* _PIT_H_ */
