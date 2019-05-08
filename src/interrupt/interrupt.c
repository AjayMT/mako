
// interrupt.c
//
// Interrupt handling interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <common/stdint.h>
#include <drivers/framebuffer/framebuffer.h>
#include "interrupt.h"

// All registered interrupt handlers.
static interrupt_handler_t registered_handlers[IDT_NUM_ENTRIES];

// Register an interrupt handler.
uint32_t register_interrupt_handler(
  uint32_t index, interrupt_handler_t handler
  )
{
  if (index >= IDT_NUM_ENTRIES || registered_handlers[index])
    return 1;

  registered_handlers[index] = handler;
  return 0;
}

// Forward interrupts to registered handler.
void forward_interrupt(
  cpu_state_t c_state, idt_info_t info, stack_state_t s_state
  )
{
  if (registered_handlers[info.idt_index] == 0) {
    // TODO Handle this better. Write to stderr or smth.
    fb_clear();
    fb_write("unhandled interrupt", 19);
    return;
  }

  registered_handlers[info.idt_index](c_state, info, s_state);
}
