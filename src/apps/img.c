
// img.c
//
// PNG Image Viewer.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <mako.h>
#include <ui.h>
#include "scancode.h"
#include "lodepng.h"

// Window state
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;

// File path
static char *path = NULL;

// 'Opening directory' state
static uint8_t opening = 0;

// Draw the image
void render_image()
{
  if (path == NULL) return;

  uint8_t *image = NULL;
  unsigned int w = 0;
  unsigned int h = 0;
  unsigned int res = lodepng_decode32_file(&image, &w, &h, path);
  if (res) {
    fprintf(stderr, "decode error %u: %s\n", res, lodepng_error_text(res));
    return;
  }

  for (uint32_t y = 0; y < window_h; ++y)
    for (uint32_t x = 0; x < window_w; ++x)
      ui_buf[(y * window_w) + x] = 0;

  double wf = (double)w / (double)window_w;
  double hf = (double)h / (double)window_h;
  double factor = wf > hf ? wf : hf;
  uint32_t rw = w / factor;
  uint32_t rh = h / factor;
  uint32_t w_padding = (window_w - rw) / 2;
  uint32_t h_padding = (window_h - rh) / 2;
  for (uint32_t y = 0; y < rh; ++y) {
    for (uint32_t x = 0; x < rw; ++x) {
      uint32_t iy = ((double)y / (double)rh) * h;
      uint32_t ix = ((double)x / (double)rw) * w;
      uint32_t r = image[4 * iy * w + 4 * ix];
      uint32_t g = image[4 * iy * w + 4 * ix + 1];
      uint32_t b = image[4 * iy * w + 4 * ix + 2];
      uint32_t a = image[4 * iy * w + 4 * ix + 3];

      r *= (double)a / 255.0;
      g *= (double)a / 255.0;
      b *= (double)a / 255.0;
      uint32_t pixel = 0;
      pixel |= (uint8_t)b;
      pixel |= ((uint8_t)g) << 8;
      pixel |= ((uint8_t)r) << 16;
      ui_buf[(y + h_padding) * window_w + x + w_padding] = pixel;
    }
  }

  ui_swap_buffers();
}

// Keyboard input handler
void keyboard_handler(uint8_t code)
{
  static uint8_t meta = 0;

  if (code & 0x80) {
    code &= 0x7F;
    if (code == KB_SC_META) meta = 0;
    return;
  }

  switch (code) {
  case KB_SC_META: meta = 1; return;
  case KB_SC_Q:    exit(0);
  case KB_SC_TAB:  if (meta) { ui_yield(); return; }
  case KB_SC_O: {
    if (fork() == 0) {
      char *args[2] = { dirname(path), NULL };
      execve("/apps/dex", args, environ);
      exit(1);
    }
    return;
  }
  }
}

// UI event handler
void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_KEYBOARD) {
    keyboard_handler(ev.code);
    return;
  }

  window_w = ev.width;
  window_h = ev.height;

  render_image();
}

int main(int argc, char *argv[])
{
  if (argc > 1) path = strdup(argv[1]);

  int32_t res = ui_acquire_window();
  if (res < 0) return 1;
  ui_buf = (uint32_t *)res;

  while (1) {
    ui_event_t ev;
    res = ui_next_event(&ev);
    if (res < 0) return 1;
    ui_handler(ev);
  }

  return 0;
}
