
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include "../common/stdint.h"
#include "../common/ui.h"

int32_t ui_acquire_window();
int32_t ui_swap_buffers();
int32_t ui_next_event(ui_event_t *);
int32_t ui_yield();
uint32_t ui_poll_events();
int32_t ui_set_wallpaper(const char *);

#endif /* _UI_H_ */
