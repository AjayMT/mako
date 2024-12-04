
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_H_
#define _UI_H_

#include "../common/stdint.h"
#include "../common/ui.h"
#include <stdbool.h>
#include <stddef.h>

int32_t ui_acquire_window(const char *);
int32_t ui_redraw_rect(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t ui_next_event(ui_event_t *);
int32_t ui_yield();
uint32_t ui_poll_events();
int32_t ui_set_wallpaper(const char *);

enum ui_font
{
  UI_FONT_LUCIDA_GRANDE,
  UI_FONT_MONACO,
  UI_FONT_CONSOLAS,
};

void ui_render_text(uint32_t *buf,
                    size_t buf_stride,
                    const char *str,
                    size_t len,
                    enum ui_font font);

void ui_measure_text(uint32_t *w, uint32_t *h, const char *str, size_t len, enum ui_font font);

struct ui_scrollview
{
  uint32_t *content_buf;
  uint32_t content_w;
  uint32_t content_h;
  uint32_t *window_buf;
  uint32_t window_w;
  uint32_t window_h;
  uint32_t window_x;
  uint32_t window_y;
};

bool ui_scrollview_init(struct ui_scrollview *view,
                        uint32_t *window_buf,
                        uint32_t window_w,
                        uint32_t window_h);
bool ui_scrollview_grow(struct ui_scrollview *view, uint32_t dw, uint32_t dh, uint32_t padding);
void ui_scrollview_redraw_rect(struct ui_scrollview *view,
                               uint32_t x,
                               uint32_t y,
                               uint32_t w,
                               uint32_t h);
void ui_scrollview_scroll(struct ui_scrollview *view, int32_t dx, int32_t dy);

#endif /* _UI_H_ */
