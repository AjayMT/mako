
// idt.c
//
// Interrupt Descriptor Table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <common/constants.h>
#include <gdt/gdt.h>
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
static idt_gate_t idt_entries[IDT_NUM_ENTRIES];

// Load the Interrupt Descriptor Table. Implemented in idt.s.
void idt_load(uint32_t);

// Interrupt handlers defined in interrupt.s -- protected mode exceptions.
void interrupt_handler_0();
void interrupt_handler_1();
void interrupt_handler_2();
void interrupt_handler_3();
void interrupt_handler_4();
void interrupt_handler_5();
void interrupt_handler_6();
void interrupt_handler_7();
void interrupt_handler_8();
void interrupt_handler_9();
void interrupt_handler_10();
void interrupt_handler_11();
void interrupt_handler_12();
void interrupt_handler_13();
void interrupt_handler_14();
void interrupt_handler_15();
void interrupt_handler_16();
void interrupt_handler_17();
void interrupt_handler_18();
void interrupt_handler_19();

// Interrupt handlers defined in interrupt.s -- IRQs.
void interrupt_handler_32();
void interrupt_handler_33();
void interrupt_handler_34();
void interrupt_handler_35();
void interrupt_handler_36();
void interrupt_handler_37();
void interrupt_handler_38();
void interrupt_handler_39();
void interrupt_handler_40();
void interrupt_handler_41();
void interrupt_handler_42();
void interrupt_handler_43();
void interrupt_handler_44();
void interrupt_handler_45();
void interrupt_handler_46();
void interrupt_handler_47();

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

#define IDT_CREATE_GATE(i)                                      \
  idt_create_gate(                                              \
    i, (uint32_t)interrupt_handler_##i, IDT_TRAP_GATE_TYPE, PL0 \
    );

  // Create IDT gates for declared interrupts.
  // Protected mode exceptions.
  IDT_CREATE_GATE(0);
  IDT_CREATE_GATE(1);
  IDT_CREATE_GATE(2);
  IDT_CREATE_GATE(3);
  IDT_CREATE_GATE(4);
  IDT_CREATE_GATE(5);
  IDT_CREATE_GATE(6);
  IDT_CREATE_GATE(7);
  IDT_CREATE_GATE(8);
  IDT_CREATE_GATE(9);
  IDT_CREATE_GATE(10);
  IDT_CREATE_GATE(11);
  IDT_CREATE_GATE(12);
  IDT_CREATE_GATE(13);
  IDT_CREATE_GATE(14);
  IDT_CREATE_GATE(15);
  IDT_CREATE_GATE(16);
  IDT_CREATE_GATE(17);
  IDT_CREATE_GATE(18);
  IDT_CREATE_GATE(19);

  // IRQs
  IDT_CREATE_GATE(32);
  IDT_CREATE_GATE(33);
  IDT_CREATE_GATE(34);
  IDT_CREATE_GATE(35);
  IDT_CREATE_GATE(36);
  IDT_CREATE_GATE(37);
  IDT_CREATE_GATE(38);
  IDT_CREATE_GATE(39);
  IDT_CREATE_GATE(40);
  IDT_CREATE_GATE(41);
  IDT_CREATE_GATE(42);
  IDT_CREATE_GATE(43);
  IDT_CREATE_GATE(44);
  IDT_CREATE_GATE(45);
  IDT_CREATE_GATE(46);
  IDT_CREATE_GATE(47);

  idt_load((uint32_t)&table_ptr);
}
