
// rtc.c
//
// Real-Time Clock interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include <stddef.h>
#include "io.h"
#include "interrupt.h"
#include "log.h"
#include "rtc.h"

static uint16_t RTC_INDEX = 0x70;
static uint16_t RTC_DATA  = 0x71;

static uint64_t time = 0;

static void rtc_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{ ++time; rtc_ack(); }

uint64_t rtc_get_time()
{ return time; }

void rtc_init()
{
  uint32_t eflags = interrupt_save_disable();

  // Select status register B and disable NMI.
  outb(0x70, 0x8B);

  // Read previous value.
  uint8_t prev = inb(0x71);

  // Select status register again (reading resets the index).
  outb(0x70, 0x8B);

  // Set bit 6 to enable periodic interrupt.
  outb(0x71, prev | 0x40);

  register_interrupt_handler(40, rtc_handler);
  interrupt_restore(eflags);
  rtc_ack();
}

void rtc_ack()
{
  uint32_t eflags = interrupt_save_disable();
  outb(0x70, 0xC);
  inb(0x71);
  interrupt_restore(eflags);
}
