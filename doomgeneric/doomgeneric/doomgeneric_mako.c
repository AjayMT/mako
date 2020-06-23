
#include "doomgeneric.h"
#include "scancode.h"
#include <ui.h>
#include <stdio.h>
#include <mako.h>
#include <stdint.h>

static uint32_t start_time = 0;
static uint8_t window_init = 0;
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = 0;

static void keyboard_handler(uint8_t code)
{
  static uint8_t meta = 0;

  if (code & 0x80) {
    code &= 0x7F;
    switch (code) {
    case KB_SC_META: meta = 0; break;
    }
    return;
  }

  switch (code) {
  case KB_SC_TAB:
    if (meta) {
      meta = 0;
      ui_yield();
      return;
    }
  }
}

static void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_KEYBOARD) {
    keyboard_handler(ev.code);
    return;
  }

  if (ev.width < DOOMGENERIC_RESX || ev.height < DOOMGENERIC_RESY)
    return;

  if (ev.width == window_w && ev.height == window_h) return;

  if (ui_buf) {
    uint32_t oldsize = window_w * window_h * 4;
    pagefree((uint32_t)ui_buf, (oldsize / 0x1000) + 1);
  }

  window_w = ev.width; window_h = ev.height;
  uint32_t size = ev.width * ev.height * 4;
  ui_buf = (uint32_t *)pagealloc((size / 0x1000) + 1);
  window_init = 1;
  start_time = systime();
}

void DG_Init()
{
  ui_init();
  ui_set_handler(ui_handler);
  int32_t res = ui_acquire_window();
  //while (res) res = ui_acquire_window();
}

void DG_DrawFrame()
{
  if (window_init == 0) return;

  for (int i = 0; i < DOOMGENERIC_RESY; ++i) {
    for (int j = 0; j < DOOMGENERIC_RESX; ++j) {
      ui_buf[i * window_w + j] = DG_ScreenBuffer[i * DOOMGENERIC_RESX + j];
    }
  }

  ui_swap_buffers((uint32_t)ui_buf);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
  return 0;
}

void DG_SleepMs(uint32_t ms)
{ msleep(ms); }

void DG_SetWindowTitle(const char *title)
{}

uint32_t DG_GetTicksMs()
{ return systime() - start_time; }
