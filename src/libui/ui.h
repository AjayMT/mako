
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include <stddef.h>
#include "../common/stdint.h"
#include "../common/ui.h"

int32_t ui_acquire_window(const char *);
int32_t ui_redraw_rect(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t ui_next_event(ui_event_t *);
int32_t ui_yield();
uint32_t ui_poll_events();
int32_t ui_set_wallpaper(const char *);

enum ui_font {
  UI_FONT_LUCIDA_GRANDE,
  UI_FONT_MONACO,
  UI_FONT_CONSOLAS,
};

void ui_render_text(uint32_t *buf, size_t buf_stride, const char *str, size_t len, enum ui_font font);

#endif /* _UI_H_ */
