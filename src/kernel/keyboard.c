
// keyboard.c
//
// Keyboard driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "io.h"
#include "interrupt.h"
#include "ui.h"
#include "keyboard.h"
#include "process.h"

static void keyboard_interrupt_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  (void)info;
  process_t *current = process_current();
  uint8_t saved_kernel_state;
  if (current) {
    saved_kernel_state = current->in_kernel;
    update_current_process_registers(cs, ss);
    current->in_kernel = 1;
  }
  uint8_t code = inb(0x60);
  ui_dispatch_keyboard_event(code);
  disable_interrupts();
  if (current) current->in_kernel = saved_kernel_state;
}

uint32_t keyboard_init()
{
  register_interrupt_handler(33, keyboard_interrupt_handler);
  return 0;
}
