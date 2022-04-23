
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

static interrupt_handler_t rtc_handler = NULL;

static void local_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  rtc_ack();
  if (rtc_handler) rtc_handler(cs, info, ss);
}

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

  // Set the rate to 9 for a frequency of 128 Hz (I think?)
  /* outb(0x70, 0x8A);
  prev = inb(0x71);
  outb(0x70, 0x8A);
  outb(0x71, (prev & 0xF0) | 9); */

  register_interrupt_handler(40, local_handler);
  interrupt_restore(eflags);
  rtc_ack();
}

void rtc_set_handler(interrupt_handler_t h)
{ rtc_handler = h; }

void rtc_ack()
{
  uint32_t eflags = interrupt_save_disable();
  outb(0x70, 0xC);
  inb(0x71);
  interrupt_restore(eflags);
}
