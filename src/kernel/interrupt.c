
// interrupt.c
//
// Interrupt handling interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "interrupt.h"
#include "../common/stdint.h"
#include "log.h"
#include "pic.h"

// All registered interrupt handlers.
static interrupt_handler_t registered_handlers[IDT_NUM_ENTRIES];

// Initialize interrupt handlers.
void interrupt_init()
{
  for (uint32_t i = 0; i < IDT_NUM_ENTRIES; ++i)
    registered_handlers[i] = 0;
}

// Register an interrupt handler.
uint32_t register_interrupt_handler(uint32_t index, interrupt_handler_t handler)
{
  if (index >= IDT_NUM_ENTRIES || registered_handlers[index])
    return 1;

  registered_handlers[index] = handler;
  return 0;
}

// Unregister an interrupt handler.
uint8_t unregister_interrupt_handler(uint32_t index)
{
  if (index >= IDT_NUM_ENTRIES)
    return 1;
  registered_handlers[index] = 0;
  return 0;
}

// Forward interrupts to registered handler.
void forward_interrupt(cpu_state_t c_state, idt_info_t info, stack_state_t s_state)
{
  // Send acknowledgement to PIC for IRQs.
  if (info.idt_index >= 32)
    pic_acknowledge(info.idt_index);

  if (registered_handlers[info.idt_index] == 0) {
    log_error("interrupt", "unhandled interrupt %u, eip %x\n", info.idt_index, s_state.eip);
    return;
  }

  registered_handlers[info.idt_index](c_state, info, s_state);
  disable_interrupts();
}
