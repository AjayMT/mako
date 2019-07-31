
// dex.c
//
// Directory EXplorer.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <mako.h>
#include <ui.h>
#include "SDL_picofont.h"

static const uint32_t BG_COLOR    = 0xffffff;
static const uint32_t TEXT_COLOR  = 0;
static const uint32_t YIELD_COLOR = 0x222222;

// Window state.
static uint32_t *ui_buf = NULL;
static uint32_t window_w = 0;
static uint32_t window_h = 0;

// Directory state.
static char *current_path = NULL;
static uint32_t top_idx = 0;
static uint32_t cursor_idx = 0;

__attribute__((always_inline))
static inline uint32_t round(double d)
{ return (uint32_t)d + (d - (uint32_t)d >= 0.5); }

static void render_text(const char *text, uint32_t x, uint32_t y)
{
  uint32_t len = strlen(text);
  FNT_xy dim = FNT_Generate(text, len, 0, NULL);
  uint32_t w = dim.x;
  uint32_t h = dim.y;
  uint8_t *pixels = malloc(w * h);
  memset(pixels, 0, w * h);
  FNT_Generate(text, len, w, pixels);

  for (uint32_t i = 0; i < w; ++i)
    for (uint32_t j = 0; j < h * 1.5; ++j)
      if (pixels[(round(j / 1.5) * w) + i])
        ui_buf[((y + j) * window_w) + x + i] = TEXT_COLOR;

  free(pixels);
}

static void render_dirname()
{
  render_text(current_path, 5, 5);
  uint32_t *line_row = ui_buf + ((10 + 12) * window_w);
  for (uint32_t i = 0; i < window_w; ++i)
    line_row[i] = TEXT_COLOR;
}

static void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_RESIZE || ev.type == UI_EVENT_WAKE) {
    if (ui_buf) {
      uint32_t oldsize = window_w * window_h * 4;
      pagefree((uint32_t)ui_buf, (oldsize / 0x1000) + 1);
    }
    uint32_t size = ev.width * ev.height * 4;
    ui_buf = (uint32_t *)pagealloc((size / 0x1000) + 1);
    window_w = ev.width;
    window_h = ev.height;

    memset(ui_buf, BG_COLOR, window_w * window_h * 4);
    render_dirname();

    ui_swap_buffers((uint32_t)ui_buf);

    return;
  }

  if (ev.code & 0x80) return; // key break

  switch (ev.code) {
  case 75: ui_split(UI_SPLIT_LEFT); break;
  case 77: ui_split(UI_SPLIT_RIGHT); break;
  case 72: ui_split(UI_SPLIT_UP); break;
  case 80: ui_split(UI_SPLIT_DOWN);
  }
}

int main(int argc, char *argv[])
{
  if (argc > 1) current_path = strdup(argv[1]);

  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();

  while (1) ui_wait();

  return 0;
}
