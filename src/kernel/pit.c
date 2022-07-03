
// pit.c
//
// Programmable Interval Timer interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "interrupt.h"
#include "io.h"
#include "pit.h"

static const uint16_t PIT_CHANNEL_0_DATA = 0x40;
static const uint16_t PIT_CHANNEL_1_DATA = 0x41;
static const uint16_t PIT_CHANNEL_2_DATA = 0x42;
static const uint16_t PIT_COMMAND        = 0x43;
static const uint32_t PIT_FREQUENCY      = 0x1234de;

// Initialize the PIT.
void pit_init()
{
  uint32_t eflags = interrupt_save_disable();
  uint8_t data = (1 << 5) | (1 << 4) | (1 << 2) | (1 << 1);
  outb(PIT_COMMAND, data);
  pit_set_interval(20);
  interrupt_restore(eflags);
}

// Set interval (in milliseconds).
void pit_set_interval(uint32_t interval)
{
  uint32_t f = 1000 / interval;
  uint16_t divisor = (uint16_t)(PIT_FREQUENCY / f);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)divisor);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)(divisor >> 8));
}
