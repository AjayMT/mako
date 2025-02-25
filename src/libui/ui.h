
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

int32_t ui_acquire_window(uint32_t *buf, const char *title, uint32_t w, uint32_t h);
int32_t ui_resize_window(uint32_t *buf, uint32_t w, uint32_t h);
int32_t ui_redraw_rect(uint32_t, uint32_t, uint32_t, uint32_t);
int32_t ui_next_event(ui_event_t *);
int32_t ui_enable_mouse_move_events();
int32_t ui_yield();
uint32_t ui_poll_events();
int32_t ui_set_wallpaper(const char *);

enum ui_font
{
  UI_FONT_TWINLEAF,
  UI_FONT_LUCIDA_GRANDE,
  UI_FONT_MONACO,
  UI_FONT_X_FIXED,
};

uint32_t ui_blend_alpha(uint32_t bg, uint32_t fg);
void ui_render_text(uint32_t *buf,
                    size_t buf_stride,
                    const char *str,
                    size_t len,
                    enum ui_font font,
                    uint32_t color);
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
  uint32_t background_color;
};

bool ui_scrollview_init(struct ui_scrollview *view,
                        uint32_t *window_buf,
                        uint32_t window_w,
                        uint32_t window_h,
                        uint32_t background_color);
bool ui_scrollview_resize(struct ui_scrollview *view, uint32_t w, uint32_t h);
void ui_scrollview_redraw_rect(struct ui_scrollview *view,
                               uint32_t x,
                               uint32_t y,
                               uint32_t w,
                               uint32_t h);
void ui_scrollview_redraw_rect_buffered(struct ui_scrollview *view,
                                        uint32_t x,
                                        uint32_t y,
                                        uint32_t w,
                                        uint32_t h);
void ui_scrollview_scroll(struct ui_scrollview *view, int32_t dx, int32_t dy);
void ui_scrollview_clear(struct ui_scrollview *view, uint32_t *new_buf, uint32_t w, uint32_t h);

#endif /* _UI_H_ */
