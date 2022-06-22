
// keyboard.c
//
// Keyboard driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "io.h"
#include "interrupt.h"
#include "../common/errno.h"
#include "fs.h"
#include "ui.h"
#include "kheap.h"
#include "util.h"
#include "log.h"
#include "keyboard.h"

static void keyboard_interrupt_handler()
{
  uint8_t code = inb(0x60);
  ui_dispatch_keyboard_event(code);
}

uint32_t keyboard_init()
{
  register_interrupt_handler(33, keyboard_interrupt_handler);
  return 0;
}
