
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ui.h"
#include "../common/stdint.h"
#include "../libc/_syscall.h"
#include "../libc/errno.h"
#include "../libc/mako.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "ui_font_data.h"
#include <stddef.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

int32_t ui_acquire_window(uint32_t *buf, const char *title, uint32_t w, uint32_t h)
{
  int32_t res = _syscall4(SYSCALL_UI_MAKE_RESPONDER, (uint32_t)buf, (uint32_t)title, w, h);
  if (res < 0) {
    errno = -res;
    return -1;
  }
  return 0;
}

int32_t ui_resize_window(uint32_t *buf, uint32_t w, uint32_t h)
{
  int32_t res = _syscall3(SYSCALL_UI_RESIZE_WINDOW, (uint32_t)buf, w, h);
  if (res < 0) {
    errno = -res;
    return -1;
  }
  return 0;
}

int32_t ui_redraw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
  int32_t res = _syscall4(SYSCALL_UI_REDRAW_RECT, x, y, w, h);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}

int32_t ui_next_event(ui_event_t *buf)
{
  int32_t res = _syscall1(SYSCALL_UI_NEXT_EVENT, (uint32_t)buf);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}

int32_t ui_enable_mouse_move_events()
{
  int32_t err = _syscall0(SYSCALL_UI_ENABLE_MOUSE_MOVE_EVENTS);
  if (err < 0) {
    errno = -err;
    err = -1;
  }
  return err;
}

int32_t ui_yield()
{
  int32_t res = _syscall0(SYSCALL_UI_YIELD);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}

uint32_t ui_poll_events()
{
  return _syscall0(SYSCALL_UI_POLL_EVENTS);
}

int32_t ui_set_wallpaper(const char *path)
{
  int32_t res = _syscall1(SYSCALL_UI_SET_WALLPAPER, (uint32_t)path);
  if (res < 0) {
    errno = -res;
    res = -1;
  }
  return res;
}

uint32_t ui_blend_alpha(uint32_t bg, uint32_t fg)
{
  const uint8_t opacity = fg >> 24;
  if (opacity == 0)
    return bg;
  if (opacity == 0xff)
    return fg;

  const uint8_t fg_b = ((fg & 0xff) * opacity) / 0xff;
  const uint8_t fg_g = (((fg >> 8) & 0xff) * opacity) / 0xff;
  const uint8_t fg_r = (((fg >> 16) & 0xff) * opacity) / 0xff;
  const uint32_t bg_b = bg & 0xff;
  const uint32_t bg_g = (bg >> 8) & 0xff;
  const uint32_t bg_r = (bg >> 16) & 0xff;

  const uint16_t t = 0xff ^ opacity;
  const uint32_t blend_g = fg_g + (((bg_g * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_b = fg_b + (((bg_b * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_r = fg_r + (((bg_r * t + 0x80) * 0x101) >> 16);

  return blend_b | (blend_g << 8) | (blend_r << 16);
}

static void render_char(uint32_t *buf,
                        size_t buf_stride,
                        struct font_char_info c,
                        const uint8_t *font_data,
                        unsigned font_height,
                        uint32_t color)
{
  for (size_t y = 0; y < font_height; ++y) {
    for (size_t x = 0; x < c.width; ++x) {
      uint8_t opacity = font_data[c.data_offset + y * c.width + x];
      buf[y * buf_stride + x] =
        ui_blend_alpha(buf[y * buf_stride + x], (opacity << 24) | (color & 0xffffff));
    }
  }
}

static void select_font(struct font_char_info **char_info,
                        uint8_t **data,
                        unsigned *height,
                        enum ui_font font)
{
  switch (font) {
    case UI_FONT_TWINLEAF:
      *char_info = twinleaf_char_info;
      *data = twinleaf_data;
      *height = TWINLEAF_HEIGHT;
      break;
    case UI_FONT_LUCIDA_GRANDE:
      *char_info = lucida_grande_char_info;
      *data = lucida_grande_data;
      *height = LUCIDA_GRANDE_HEIGHT;
      break;
    case UI_FONT_MONACO:
      *char_info = monaco_char_info;
      *data = monaco_data;
      *height = MONACO_HEIGHT;
      break;
    case UI_FONT_X_FIXED:
      *char_info = x_fixed_char_info;
      *data = x_fixed_data;
      *height = X_FIXED_HEIGHT;
      break;
  }
}

void ui_render_text(uint32_t *buf,
                    size_t buf_stride,
                    const char *str,
                    size_t len,
                    enum ui_font font,
                    uint32_t color)
{
  struct font_char_info *font_char_info;
  uint8_t *font_data;
  unsigned font_height;
  select_font(&font_char_info, &font_data, &font_height, font);

  size_t x = 0;
  size_t y = 0;
  for (size_t i = 0; i < len; ++i) {
    if (x >= buf_stride)
      break;

    char c = 0;
    switch (str[i]) {
      case '\n':
        y += font_height;
        x = 0;
        break;
      case '\t':
        x += 4 * font_char_info[' ' - 32].width;
        break;
      default:
        c = str[i];
    }

    if (c < 32 || c > 126)
      continue;

    uint32_t *p = buf + (y * buf_stride) + x;
    const struct font_char_info char_info = font_char_info[c - 32];
    render_char(p, buf_stride, char_info, font_data, font_height, color);
    x += char_info.width;
  }
}

void ui_measure_text(uint32_t *w, uint32_t *h, const char *str, size_t len, enum ui_font font)
{
  struct font_char_info *font_char_info;
  uint8_t *font_data;
  unsigned font_height;
  select_font(&font_char_info, &font_data, &font_height, font);

  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t max_w = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = 0;
    switch (str[i]) {
      case '\n':
        y += font_height;
        x = 0;
        break;
      case '\t':
        x += 4 * font_char_info[' ' - 32].width;
        break;
      default:
        c = str[i];
    }

    if (c < 32 || c > 126)
      continue;

    const struct font_char_info char_info = font_char_info[c - 32];
    x += char_info.width;
    max_w = max(max_w, x);
  }

  *w = max_w;
  *h = y + font_height;
}

#define SCROLLBAR_SIZE 8

static void draw_scrollbars(struct ui_scrollview *view)
{
  const uint32_t background_color = 0xcccccc;
  const uint32_t scrollbar_color = view->background_color;

  if (view->window_h != view->content_h) {
    uint32_t y_fill_start = (view->window_y * (view->window_h - SCROLLBAR_SIZE)) / view->content_h;
    uint32_t y_fill_height = (view->window_h * (view->window_h - SCROLLBAR_SIZE)) / view->content_h;
    for (uint32_t y = 0; y < view->window_h - SCROLLBAR_SIZE; ++y) {
      uint32_t *bufp = view->window_buf + y * view->window_w + view->window_w - SCROLLBAR_SIZE;
      if (y == 0 || y == view->window_h - SCROLLBAR_SIZE - 1) {
        memset32(bufp, background_color, SCROLLBAR_SIZE);
        continue;
      }
      bufp[0] = background_color;
      if (y >= y_fill_start && y < y_fill_start + y_fill_height)
        memset32(bufp + 1, scrollbar_color, SCROLLBAR_SIZE - 2);
      else
        memset32(bufp + 1, background_color, SCROLLBAR_SIZE - 2);
      bufp[SCROLLBAR_SIZE - 1] = background_color;
    }
  }

  if (view->window_w != view->content_w) {
    uint32_t x_fill_start = (view->window_x * (view->window_w - SCROLLBAR_SIZE)) / view->content_w;
    uint32_t x_fill_width = (view->window_w * (view->window_w - SCROLLBAR_SIZE)) / view->content_w;
    for (uint32_t y = view->window_h - SCROLLBAR_SIZE; y < view->window_h; ++y) {
      uint32_t *bufp = view->window_buf + y * view->window_w;
      if (y == view->window_h - SCROLLBAR_SIZE || y == view->window_h - 1) {
        memset32(bufp, background_color, view->window_w - SCROLLBAR_SIZE);
        continue;
      }
      memset32(bufp, background_color, view->window_w - SCROLLBAR_SIZE);
      memset32(bufp + x_fill_start, scrollbar_color, x_fill_width);
      bufp[0] = background_color;
      bufp[view->window_w - SCROLLBAR_SIZE - 1] = background_color;
    }
  }
}

bool ui_scrollview_init(struct ui_scrollview *view,
                        uint32_t *window_buf,
                        uint32_t window_w,
                        uint32_t window_h,
                        uint32_t background_color)
{
  view->content_w = window_w;
  view->content_h = window_h;
  view->content_buf = malloc(view->content_w * view->content_h * sizeof(uint32_t));
  if (view->content_buf == NULL) {
    errno = ENOMEM;
    return false;
  }
  view->window_buf = window_buf;
  view->window_w = window_w;
  view->window_h = window_h;
  view->window_x = 0;
  view->window_y = 0;
  view->background_color = background_color;

  memset32(view->content_buf, background_color, view->content_w * view->content_h);
  memset32(view->window_buf, background_color, view->window_w * view->window_h);
  draw_scrollbars(view);

  return true;
}

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

bool ui_scrollview_resize(struct ui_scrollview *view, uint32_t w, uint32_t h)
{
  w = max(w, view->window_w);
  h = max(h, view->window_h);

  uint32_t *new_buf = malloc(w * h * sizeof(uint32_t));
  if (new_buf == NULL)
    return false;

  memset32(new_buf, view->background_color, w * h);
  uint32_t min_w = min(w, view->content_w);
  uint32_t min_h = min(h, view->content_h);
  for (uint32_t y = 0; y < min_h; ++y)
    memcpy32(new_buf + y * w, view->content_buf + y * view->content_w, min_w);

  free(view->content_buf);
  view->content_buf = new_buf;
  view->content_w = w;
  view->content_h = h;
  draw_scrollbars(view);
  return true;
}

void ui_scrollview_redraw_rect(struct ui_scrollview *view,
                               uint32_t x,
                               uint32_t y,
                               uint32_t w,
                               uint32_t h)
{
  uint32_t clamped_x = max(x, view->window_x);
  uint32_t clamped_y = max(y, view->window_y);

  if (clamped_x >= view->window_x + view->window_w ||
      clamped_y >= view->window_y + view->window_h || clamped_x >= x + w || clamped_y >= y + h)
    return;

  uint32_t clamped_w = min(x + w, view->window_x + view->window_w) - clamped_x;
  uint32_t clamped_h = min(y + h, view->window_y + view->window_h) - clamped_y;

  for (uint32_t i = clamped_y; i < clamped_y + clamped_h; ++i) {
    uint32_t *contentp = view->content_buf + i * view->content_w + clamped_x;
    uint32_t *windowp =
      view->window_buf + (i - view->window_y) * view->window_w + clamped_x - view->window_x;
    memcpy32(windowp, contentp, clamped_w);
  }

  if (clamped_x + clamped_w >= view->window_w - SCROLLBAR_SIZE ||
      clamped_y + clamped_h >= view->window_h - SCROLLBAR_SIZE) {
    draw_scrollbars(view);
    ui_redraw_rect(0, 0, view->window_w, view->window_h);
  } else
    ui_redraw_rect(clamped_x - view->window_x, clamped_y - view->window_y, clamped_w, clamped_h);
}

void ui_scrollview_redraw_rect_buffered(struct ui_scrollview *view,
                                        uint32_t x,
                                        uint32_t y,
                                        uint32_t w,
                                        uint32_t h)
{
  uint32_t clamped_x = max(x, view->window_x);
  uint32_t clamped_y = max(y, view->window_y);

  if (clamped_x >= view->window_x + view->window_w ||
      clamped_y >= view->window_y + view->window_h || clamped_x >= x + w || clamped_y >= y + h)
    return;

  uint32_t clamped_w = min(x + w, view->window_x + view->window_w) - clamped_x;
  uint32_t clamped_h = min(y + h, view->window_y + view->window_h) - clamped_y;

  for (uint32_t i = clamped_y; i < clamped_y + clamped_h; ++i) {
    uint32_t *contentp = view->content_buf + i * view->content_w + clamped_x;
    uint32_t *windowp =
      view->window_buf + (i - view->window_y) * view->window_w + clamped_x - view->window_x;
    memcpy32(windowp, contentp, clamped_w);
  }

  if (clamped_x + clamped_w >= view->window_w - SCROLLBAR_SIZE ||
      clamped_y + clamped_h >= view->window_h - SCROLLBAR_SIZE)
    draw_scrollbars(view);
}

void ui_scrollview_scroll(struct ui_scrollview *view, int32_t dx, int32_t dy)
{
  bool scrolled = false;
  if (view->window_x + dx < view->content_w - view->window_w) {
    view->window_x += dx;
    scrolled = true;
  }
  if (view->window_y + dy < view->content_h - view->window_h) {
    view->window_y += dy;
    scrolled = true;
  }

  if (!scrolled)
    return;

  for (uint32_t y = 0; y < view->window_h; ++y) {
    uint32_t *windowp = view->window_buf + y * view->window_w;
    uint32_t *contentp =
      view->content_buf + (y + view->window_y) * view->content_w + view->window_x;
    memcpy32(windowp, contentp, view->window_w);
  }
  draw_scrollbars(view);
  ui_redraw_rect(0, 0, view->window_w, view->window_h);
}
