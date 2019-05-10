
// keyboard.h
//
// Keyboard driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include <common/stdint.h>

extern const uint32_t KEYBOARD_INTERRUPT_INDEX;

// Initialize the keyboard driver.
void keyboard_init();

#endif /* _KEYBOARD_H_ */
