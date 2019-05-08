
// idt.c
//
// Interrupt Descriptor Table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <common/stdint.h>
#include <common/constants.h>
#include "idt.h"

// TODO documentation.

// IDT Entry struct.
struct idt_gate_s {
  uint16_t offset_1;  // Lower 16 bits of offset.
  uint16_t selector;  // Code segment selector in the GDT.
  uint8_t zero;       // Unused, set to 0.
  uint8_t type_attr;  // Type and attributes.
  uint16_t offset_2;  // Higher 16 bits of offset.
} __attribute__((packed));
typedef struct idt_gate_s idt_gate_t;

// Struct into which LIDT loads the IDT.
struct idt_ptr_s {
  uint16_t limit;  // Size of the IDT.
  uint32_t base;   // Base address of the IDT.
} __attribute__((packed));
typedef struct idt_ptr_s idt_ptr_t;

// Constants.
static const uint8_t IDT_INTERRUPT_GATE_TYPE       = 0;
static const uint8_t IDT_TRAP_GATE_TYPE            = 1;
static const uint32_t IDT_TIMER_INTERRUPT_INDEX    = 0x20;
static const uint32_t IDT_KEYBOARD_INTERRUPT_INDEX = 0x21;

// The IDT.
#define IDT_NUM_ENTRIES 256
static idt_gate_t idt_entries[IDT_NUM_ENTRIES];

// Load the Interrupt Descriptor Table. Implemented in idt.s.
void idt_load(uint32_t);

// Create an entry in the table.
static void idt_create_gate(
  uint32_t index, uint32_t offset, uint8_t type,uint8_t privilege_level
  )
{
  idt_entries[index].offset_1 = offset & 0x0000FFFF;
  idt_entries[index].offset_2 = (offset >> 16) & 0x0000FFFF;
  idt_entries[index].selector = SEGMENT_SELECTOR_KERNEL_CS;
  idt_entries[index].zero = 0;

  // TODO document this.
  idt_entries[index].type_attr =
    (0x01 << 7)
    | ((privilege_level & 0x03) << 5)
    | (0x01 << 3)
    | (0x01 << 2)
    | (0x01 << 1)
    | type;
}

// Initialize the IDT.
void idt_init()
{
  idt_ptr_t table_ptr;
  table_ptr.limit = sizeof(idt_gate_t) * IDT_NUM_ENTRIES;
  table_ptr.base = (uint32_t)&idt_entries;

  idt_load((uint32_t)&table_ptr);
}
