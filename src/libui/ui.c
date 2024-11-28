
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "../libc/stdlib.h"
#include "../libc/mako.h"
#include "../libc/errno.h"
#include "../libc/_syscall.h"
#include "ui.h"

struct font_char_info {
  unsigned width;
  unsigned data_offset;
};

#include "ui_font_data.h"

int32_t ui_acquire_window()
{
  // ((SCREENWIDTH / 2) * (SCREENHEIGHT / 2) * 4) / 0x1000
  uint32_t buf = pagealloc(((SCREENWIDTH >> 1) * (SCREENHEIGHT >> 1)) >> 10);
  if (buf == 0) return -1;

  int32_t res = _syscall1(SYSCALL_UI_MAKE_RESPONDER, buf);
  if (res < 0) { errno = -res; return -1; }
  return buf;
}

int32_t ui_swap_buffers()
{
  int32_t res = _syscall0(SYSCALL_UI_SWAP_BUFFERS);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_next_event(ui_event_t *buf)
{
  int32_t res = _syscall1(SYSCALL_UI_NEXT_EVENT, (uint32_t)buf);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_yield()
{
  int32_t res = _syscall0(SYSCALL_UI_YIELD);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

uint32_t ui_poll_events()
{ return _syscall0(SYSCALL_UI_POLL_EVENTS); }

int32_t ui_set_wallpaper(const char *path)
{
  int32_t res = _syscall1(SYSCALL_UI_SET_WALLPAPER, (uint32_t)path);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

static void render_char(uint32_t *buf, size_t buf_stride,
                        struct font_char_info c, const uint8_t *font_data, unsigned font_height)
{
  for (size_t y = 0; y < font_height; ++y) {
    for (size_t x = 0; x < c.width; ++x) {
      uint8_t byte = 0xff - font_data[c.data_offset + y * c.width + x];
      buf[y * buf_stride + x] = (byte << 16) | (byte << 8) | byte;
    }
  }
}

static void select_font(struct font_char_info **char_info, uint8_t **data, unsigned *height, enum ui_font font)
{
  switch (font) {
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
    case UI_FONT_CONSOLAS:
      *char_info = consolas_char_info;
      *data = consolas_data;
      *height = CONSOLAS_HEIGHT;
      break;
  }
}

void ui_render_text(uint32_t *buf, size_t buf_stride, const char *str, size_t len, enum ui_font font)
{
  struct font_char_info *font_char_info;
  uint8_t *font_data;
  unsigned font_height;
  select_font(&font_char_info, &font_data, &font_height, font);

  size_t x = 0;
  size_t y = 0;
  for (size_t i = 0; i < len; ++i) {
    if (x >= buf_stride) break;

    char c = 0;
    switch (str[i]) {
    case '\n': y += font_height; x = 0; break;
    case '\t': x += 4 * font_char_info[' ' - 32].width; break;
    default: c = str[i];
    }

    if (c < 32 || c > 126) continue;

    uint32_t *p = buf + (y * buf_stride) + x;
    const struct font_char_info char_info = font_char_info[c - 32];
    render_char(p, buf_stride, char_info, font_data, font_height);
    x += char_info.width;
  }
}
