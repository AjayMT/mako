
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include "../common/stdint.h"
#include "process.h"
#include "ds.h"
#include "../common/ui.h"

uint32_t ui_init(uint32_t);
uint32_t ui_dispatch_keyboard_event(uint8_t);
uint32_t ui_make_responder(process_t *, uint32_t);
uint32_t ui_kill(process_t *);
uint32_t ui_swap_buffers(process_t *);
uint32_t ui_yield(process_t *);
uint32_t ui_next_event(process_t *, uint32_t);
uint32_t ui_poll_events(process_t *);

#endif /* _UI_H_ */
