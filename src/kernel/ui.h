
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

typedef struct ui_responder_s {
  process_t *process;
  ui_window_t window;
  list_node_t *list_node;
  volatile uint32_t lock;
} ui_responder_t;

uint32_t ui_init(uint32_t);
uint32_t ui_dispatch_keyboard_event(uint8_t);
uint32_t ui_split(process_t *, ui_split_type_t);
uint32_t ui_make_responder(process_t *);
uint32_t ui_kill(process_t *);
uint32_t ui_swap_buffers(process_t *, uint32_t);
uint32_t ui_yield(process_t *);

#endif /* _UI_H_ */
