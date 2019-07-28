
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include <stdint.h>
#include <_ui_common.h>

typedef void (*ui_event_handler_t)(ui_event_t);

int32_t ui_init();
void ui_set_handler(ui_event_handler_t);
int32_t ui_acquire_window();
int32_t ui_split(ui_split_type_t);
int32_t ui_swap_buffers(uint32_t);
void ui_wait();
int32_t ui_yield();

#endif /* _UI_H_ */
