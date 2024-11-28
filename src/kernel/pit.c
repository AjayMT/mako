
// pit.c
//
// Programmable Interval Timer interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "pit.h"
#include "../common/stdint.h"
#include "interrupt.h"
#include "io.h"
#include "log.h"
#include <stddef.h>

static const uint16_t PIT_CHANNEL_0_DATA = 0x40;
static const uint16_t PIT_CHANNEL_1_DATA = 0x41;
static const uint16_t PIT_CHANNEL_2_DATA = 0x42;
static const uint16_t PIT_COMMAND = 0x43;
static const uint32_t PIT_FREQUENCY = 0x1234de;

static interrupt_handler_t handler = NULL;
static uint32_t interval = 10;
static uint64_t ticks = 0;

static void tick(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  ++ticks;
  if (handler)
    handler(cs, info, ss);
}

// Initialize the PIT.
void pit_init()
{
  uint32_t eflags = interrupt_save_disable();
  uint8_t data = (1 << 5) | (1 << 4) | (1 << 2) | (1 << 1);
  outb(PIT_COMMAND, data);
  pit_set_interval(interval);
  register_interrupt_handler(32, tick);
  interrupt_restore(eflags);
}

// Get and set interval (in milliseconds).
uint32_t pit_get_interval()
{
  return interval;
}
void pit_set_interval(uint32_t i)
{
  interval = i;
  uint32_t f = 1000 / i;
  uint16_t divisor = (uint16_t)(PIT_FREQUENCY / f);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)divisor);
  outb(PIT_CHANNEL_0_DATA, (uint8_t)(divisor >> 8));
}

uint64_t pit_get_time()
{
  return ticks * interval;
}

// Set interrupt handler.
void pit_set_handler(interrupt_handler_t h)
{
  handler = h;
}
