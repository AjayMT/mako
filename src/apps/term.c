
// term.c
//
// Terminal emulator and shell.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include <mako.h>
#include <ui.h>
#include <string.h>

static uint32_t *ui_buf = NULL;

void keyboard_handler(uint8_t code)
{
  (void)code;
}

void resize_handler(ui_event_t ev)
{
  memset(ui_buf, 0xff, ev.width * ev.height * sizeof(uint32_t));

  const char *str = "dex pie term img doomgeneric. int main(int argc, char *argv[]) {}";
  ui_render_text(ui_buf + 10 * ev.width + 10, ev.width, str, strlen(str), UI_FONT_LUCIDA_GRANDE);
  ui_render_text(ui_buf + 30 * ev.width + 10, ev.width, str, strlen(str), UI_FONT_MONACO);

  ui_swap_buffers();
}

int main(int argc, char *argv[])
{
  priority(1);

  int32_t res = ui_acquire_window("term");
  if (res < 0) return 1;
  ui_buf = (uint32_t *)res;

  while (1) {
    ui_event_t ev;
    res = ui_next_event(&ev);
    if (res < 0) return 1;
    switch (ev.type) {
    case UI_EVENT_KEYBOARD:
      keyboard_handler(ev.code);
      break;
    case UI_EVENT_WAKE:
    case UI_EVENT_RESIZE:
      resize_handler(ev);
      break;
    default:;
    }
  }

  return 0;
}
