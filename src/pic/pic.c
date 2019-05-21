
// pic.c
//
// Programmable interrupt controller interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <drivers/io/io.h>
#include "pic.h"

// See https://wiki.osdev.org/PIC#Programming_the_PIC_chips for
// an explanation of this stuff.

// Constants.
static const uint8_t PIC1_PORT_A = 0x20;
static const uint8_t PIC1_PORT_B = 0x21;
static const uint8_t PIC2_PORT_A = 0xA0;
static const uint8_t PIC2_PORT_B = 0xA1;
static const uint8_t PIC1_ICW1   = 0x11; // Initialize the PIC and enable ICW4.
static const uint8_t PIC1_ICW2   = 0x20; // IRQs 0-7 remapped to IDT index 32-39.
static const uint8_t PIC2_ICW1   = PIC1_ICW2;
static const uint8_t PIC2_ICW2   = 0x28; // IRQs 8-15 remapped to IDT index 40-47.
static const uint8_t PIC1_ICW3   = 0x04; // PIC1 connected to PIC2 via IRQ2.
static const uint8_t PIC2_ICW3   = 0x02; // PIC2 connected to PIC1 via IRQ1.
static const uint8_t PIC1_ICW4   = 0x05; // Enable 8086/88 mode, PIC1 is master.
static const uint8_t PIC2_ICW4   = 0x01; // Enable 8086/88 mode.
static const uint8_t PIC_EOI     = 0x20; // End-of-interrupt.

// Initialize the PICs.
void pic_init()
{
  unsigned char mask1, mask2;
  mask1 = inb(PIC1_PORT_B);
  mask2 = inb(PIC2_PORT_B);

  // ICW1
  outb(PIC1_PORT_A, PIC1_ICW1);
  outb(PIC2_PORT_A, PIC2_ICW1);

  // ICW2
  outb(PIC1_PORT_B, PIC1_ICW2);
  outb(PIC2_PORT_B, PIC2_ICW2);

  // ICW3
  outb(PIC1_PORT_B, PIC1_ICW3);
  outb(PIC2_PORT_B, PIC2_ICW3);

  // ICW4
  outb(PIC1_PORT_B, PIC1_ICW4);
  outb(PIC2_PORT_B, PIC2_ICW4);

  pic_mask(mask1, mask2);
}

// Send acknowledgement back to the PICs.
void pic_acknowledge(uint8_t irq)
{
  if (irq >= 0x28)
    outb(PIC2_PORT_A, PIC_EOI);
  outb(PIC1_PORT_A, PIC_EOI);
}

// Set PIC masks. Each bit is an index of an IRQ, masked IRQs
// are ignpred.
void pic_mask(uint8_t mask1, uint8_t mask2)
{
  outb(PIC1_PORT_B, mask1);
  outb(PIC2_PORT_B, mask2);
}
