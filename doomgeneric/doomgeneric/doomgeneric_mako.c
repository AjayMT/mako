
#include "doomgeneric.h"
#include "doomkeys.h"
#include "scancode.h"
#include <ui.h>
#include <stdio.h>
#include <mako.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

static uint32_t start_time = 0;
static uint8_t window_init = 0;
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = 0;

#define KEYQUEUE_SIZE 512

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static unsigned char convert_to_doom_key(uint8_t key)
{
  switch (key) {
  case KB_SC_ESC:   return KEY_ESCAPE;
  case KB_SC_ENTER: return KEY_ENTER;

  case KB_SC_UP:    return KEY_UPARROW;
  case KB_SC_DOWN:  return KEY_DOWNARROW;
  case KB_SC_LEFT:  return KEY_LEFTARROW;
  case KB_SC_RIGHT: return KEY_RIGHTARROW;
  }
  return key;
}

static void add_key_to_queue(int pressed, unsigned int code) {
  unsigned char key = convert_to_doom_key(code);
  unsigned short data = (pressed << 8) | key;
  s_KeyQueue[s_KeyQueueWriteIndex] = data;
  s_KeyQueueWriteIndex++;
  s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}

static void keyboard_handler(uint8_t code)
{
  static uint8_t meta = 0;

  if (code & 0x80) {
    code &= 0x7F;
    if (code == KB_SC_META) {
      meta = 0; return;
    }
    add_key_to_queue(0, code);
    return;
  }

  switch (code) {
  case KB_SC_META: meta = 1; return;
  case KB_SC_TAB:
    if (meta) {
      meta = 0;
      ui_yield();
      return;
    }
  default:
    add_key_to_queue(1, code);
  }
}

static void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_KEYBOARD) {
    keyboard_handler(ev.code);
    return;
  }

  if (ev.width == window_w && ev.height == window_h) return;

  if (ui_buf) {
    uint32_t oldsize = window_w * window_h * 4;
    pagefree((uint32_t)ui_buf, (oldsize / 0x1000) + 1);
  } else {
    window_init = 1;
    start_time = systime();
  }

  window_w = ev.width; window_h = ev.height;
  uint32_t size = ev.width * ev.height * 4;
  ui_buf = (uint32_t *)pagealloc((size / 0x1000) + 1);
  memset(ui_buf, 0, size);
}

void DG_Init()
{
  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();
}

void DG_DrawFrame()
{
  if (window_init == 0) return;

  double min_ratio = (double)window_h / (double)DOOMGENERIC_RESY;
  if ((double)window_w / (double)DOOMGENERIC_RESX < min_ratio)
    min_ratio = (double)window_w / (double)DOOMGENERIC_RESX;
  if (min_ratio > 1) min_ratio = 1; // don't upscale for now -- maybe change later?

  for (int i = 0; i < DOOMGENERIC_RESY; ++i) {
    for (int j = 0; j < DOOMGENERIC_RESX; ++j) {
      uint32_t x = j * min_ratio;
      uint32_t y = i * min_ratio;
      ui_buf[y * window_w + x] = DG_ScreenBuffer[i * DOOMGENERIC_RESX + j];
    }
  }

  ui_swap_buffers((uint32_t)ui_buf);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
    return 0;
  else {
    unsigned short data = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;
    *pressed = data >> 8;
    *key = data & 0xFF;
    return 1;
  }
}

void DG_SleepMs(uint32_t ms)
{ msleep(ms); }

void DG_SetWindowTitle(const char *title)
{}

uint32_t DG_GetTicksMs()
{ return systime() - start_time; }
