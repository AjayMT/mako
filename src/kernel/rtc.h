
// rtc.h
//
// Real-Time Clock interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _RTC_H_
#define _RTC_H_

#include "../common/stdint.h"
#include "interrupt.h"

void rtc_init();
void rtc_ack();
void rtc_set_handler(interrupt_handler_t);

#endif /* _RTC_H_ */
