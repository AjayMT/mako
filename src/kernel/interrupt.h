
// interrupt.h
//
// Interrupt handling interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _INTERRUPT_H_
#define _INTERRUPT_H_

#include "../common/stdint.h"
#include "idt.h"

// IDT info struct.
struct idt_info_s {
  uint32_t idt_index;
  uint32_t error_code;
} __attribute__((packed));
typedef struct idt_info_s idt_info_t;

// CPU state struct.
struct cpu_state_s {
  uint32_t ds;
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
} __attribute__((packed));
typedef struct cpu_state_s cpu_state_t;

// Stack state struct.
struct stack_state_s {
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t user_esp;
  uint32_t user_ss;
} __attribute__((packed));
typedef struct stack_state_s stack_state_t;

// Interrupt handler type.
typedef void (*interrupt_handler_t)(cpu_state_t, idt_info_t, stack_state_t);

// Initialize interrupt handlers.
void interrupt_init();

// Register an interrupt handler.
uint32_t register_interrupt_handler(uint32_t, interrupt_handler_t);

// Unregister an interrupt handler.
uint8_t unregister_interrupt_handler(uint32_t);

// Forward interrupts to registered handler.
void forward_interrupt(cpu_state_t, idt_info_t, stack_state_t);

// Enable/disable interrupts. Implemented in interrupt.s.
void enable_interrupts();
void disable_interrupts();
uint32_t interrupt_save_disable();
void interrupt_restore(uint32_t);

#endif /* _INTERRUPT_H_ */
