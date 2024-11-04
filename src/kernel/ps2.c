
// ps2.c
//
// PS2 Keyboard/Mouse driver for Mako.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "ps2.h"
#include "io.h"
#include "interrupt.h"
#include "ui.h"
#include "log.h"

static void keyboard_interrupt_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  (void)info;
  uint8_t code = inb(0x60);
  ui_dispatch_keyboard_event(code);
}

uint32_t ps2_init()
{
  register_interrupt_handler(33, keyboard_interrupt_handler);
  return 0;
}
