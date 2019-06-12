
// pit.c
//
// Programmable Interval Timer interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <drivers/io/io.h>
#include "pit.h"

static const uint16_t PIT_CHANNEL_0_DATA = 0x40;
static const uint16_t PIT_CHANNEL_1_DATA = 0x41;
static const uint16_t PIT_CHANNEL_2_DATA = 0x42;
static const uint16_t PIT_COMMAND        = 0x43;
static const uint32_t PIT_FREQUENCY      = 0x1234de;

// Initialize the PIT.
void pit_init()
{
  uint8_t data = (1 << 5) | (1 << 4) | (1 << 2) | 1;
  outb(PIT_COMMAND, data);
}

// Set interval.
void pit_set_interval(uint32_t interval)
{
  uint16_t divisor = (uint16_t)((PIT_FREQUENCY * interval) / 1000);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)divisor);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)(divisor >> 8));
}
