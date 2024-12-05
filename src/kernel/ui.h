
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include "../common/stdint.h"
#include "../common/ui.h"
#include "ds.h"
#include "process.h"

uint32_t ui_init(uint32_t);
uint32_t ui_handle_keyboard_event(uint8_t);
uint32_t ui_handle_mouse_event(int32_t dx,
                               int32_t dy,
                               uint8_t left_button,
                               uint8_t right_button,
                               int8_t vscroll,
                               int8_t hscroll);
uint32_t ui_make_responder(process_t *p, uint32_t buf, const char *title, uint32_t w, uint32_t h);
uint32_t ui_kill(process_t *);
uint32_t ui_redraw_rect(process_t *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t ui_yield(process_t *);
uint32_t ui_next_event(process_t *, uint32_t);
uint32_t ui_poll_events(process_t *);
uint32_t ui_set_wallpaper(const char *);

#endif /* _UI_H_ */
