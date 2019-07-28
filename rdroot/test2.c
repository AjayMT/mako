
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <mako.h>
#include <ui.h>

static uint32_t *ui_buf = NULL;
static uint32_t window_w = 0;
static uint32_t window_h = 0;

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

    uint32_t *pos = ui_buf;
    for (uint32_t i = 0; i < ev.height; ++i) {
      for (uint32_t j = 0; j < ev.width; ++j)
        pos[j] = ((i*i) + (j*j)) < 10000 ? 0x442288 : 0xffffff;
      pos += ev.width;
    }
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
  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();

  while (1) ui_wait();
  //while (1) yield();

  return 0;
}
