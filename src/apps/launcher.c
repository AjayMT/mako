
// launcher.c
//
// App launcher.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "launcher_icons.h"
#include <mako.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>
#include <unistd.h>

const uint32_t background_color = 0xd3ccbd;
const uint32_t button_light_color = 0xe9e6de;
const uint32_t button_dark_color = 0x9f8e6d;
const uint32_t text_color = 0;
const uint32_t icon_h_padding = 8;
const uint32_t icon_v_padding = 4;
const uint32_t text_height = 16;
const uint32_t button_width = ICON_WIDTH + 2 * icon_h_padding;
const uint32_t button_height = ICON_HEIGHT + text_height + icon_v_padding;

static uint32_t *ui_buf = NULL;
static uint32_t window_w = 0;
static uint32_t window_h = 0;

static int32_t hovered_app = -1;

struct app
{
  char *name;
  const uint32_t *icon;
  char *description;
  void (*handler)(const struct app *app);
};

void launch(const struct app *app);
void launch_doom(const struct app *app);

static const struct app apps[] = {
  { "files", FILES_ICON_PIXELS, "Filesystem browser", launch },
  { "term", TERM_ICON_PIXELS, "UNIX-like shell environment", launch },
  { "write", WRITE_ICON_PIXELS, "Text editor", launch },
  { "DOOM", DOOM_ICON_PIXELS, "Shareware DOOM", launch_doom },
};
const unsigned num_apps = 4;

void draw_app_icon(unsigned idx)
{
  const struct app *app = &apps[idx];
  const uint32_t x = icon_h_padding + button_width * idx;

  for (uint32_t icon_y = 0; icon_y < ICON_HEIGHT; ++icon_y) {
    uint32_t *bufptr = ui_buf + (icon_y + icon_v_padding) * window_w;
    const uint32_t *iconptr = app->icon + icon_y * ICON_WIDTH;
    for (uint32_t icon_x = 0; icon_x < ICON_WIDTH; ++icon_x)
      bufptr[x + icon_x + icon_h_padding] =
        ui_blend_alpha(bufptr[x + icon_x + icon_h_padding], iconptr[icon_x]);
  }
  uint32_t w, h;
  const size_t name_len = strlen(app->name);
  ui_measure_text(&w, &h, app->name, name_len, UI_FONT_TWINLEAF);
  uint32_t justify_center = (button_width - w) / 2;
  uint32_t *bufptr = ui_buf + (icon_v_padding + ICON_HEIGHT) * window_w;
  ui_render_text(
    bufptr + x + justify_center, window_w, app->name, name_len, UI_FONT_TWINLEAF, text_color);
}

void launch(const struct app *app)
{
  if (fork() == 0) {
    char app_path[256];
    snprintf(app_path, sizeof(app_path), "%s/%s", getenv("APPS_PATH"), app->name);
    char *args[] = { NULL };
    execve(app_path, args, environ);
    exit(1);
  }
}

void launch_doom(const struct app *app)
{
  if (fork() == 0) {
    char app_path[256];
    snprintf(app_path, sizeof(app_path), "%s/doomgeneric", getenv("APPS_PATH"));
    char *args[] = { "-iwad", "/home/doom1.wad", NULL };
    execve(app_path, args, environ);
    exit(1);
  }
}

void handle_mouse_move(int32_t x, int32_t y)
{
  bool hovering_on_app = true;

  if (x < 0 || y < 0)
    hovering_on_app = false;
  if (x >= (int32_t)window_w || y >= (int32_t)window_h)
    hovering_on_app = false;
  if ((uint32_t)y >= window_h - text_height)
    hovering_on_app = false;
  if ((uint32_t)x < icon_h_padding || (uint32_t)x >= window_w - icon_h_padding)
    hovering_on_app = false;

  int32_t new_hovered_app = -1;
  if (hovering_on_app)
    new_hovered_app = (x - icon_h_padding) / button_width;

  if (hovered_app != -1 && hovered_app != new_hovered_app) {
    for (uint32_t y = 0; y < button_height; ++y) {
      uint32_t *bufptr = ui_buf + y * window_w + button_width * hovered_app + icon_h_padding;
      if (y < 2) {
        memset32(bufptr, background_color, button_width);
        continue;
      }
      if (y >= button_height - 2) {
        memset32(bufptr, background_color, button_width);
        continue;
      }
      bufptr[0] = background_color;
      bufptr[1] = background_color;
      bufptr[button_width - 2] = background_color;
      bufptr[button_width - 1] = background_color;
    }
    memset32(ui_buf + button_height * window_w, background_color, text_height * window_w);
  }

  if (new_hovered_app != -1 && hovered_app != new_hovered_app) {
    for (uint32_t y = 0; y < button_height; ++y) {
      uint32_t *bufptr = ui_buf + y * window_w + button_width * new_hovered_app + icon_h_padding;
      bufptr[button_width - 2] = button_dark_color;
      bufptr[button_width - 1] = button_dark_color;
      if (y < 2) {
        memset32(bufptr, button_light_color, button_width - 2);
        continue;
      }
      if (y >= button_height - 2) {
        memset32(bufptr, button_dark_color, button_width);
        continue;
      }
      bufptr[0] = button_light_color;
      bufptr[1] = button_light_color;
    }

    char *desc = apps[new_hovered_app].description;
    size_t desc_len = strlen(desc);
    uint32_t w, h;
    ui_measure_text(&w, &h, desc, desc_len, UI_FONT_TWINLEAF);
    uint32_t offset = (window_w - w) / 2;
    ui_render_text(ui_buf + button_height * window_w + offset,
                   window_w,
                   desc,
                   desc_len,
                   UI_FONT_TWINLEAF,
                   text_color);
  }

  if (hovered_app != new_hovered_app)
    ui_redraw_rect(0, 0, window_w, window_h);

  hovered_app = new_hovered_app;
}

void handle_mouse_click(int32_t x, int32_t y)
{
  if (hovered_app == -1)
    return;

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr = ui_buf + y * window_w + button_width * hovered_app + icon_h_padding;
    bufptr[button_width - 2] = button_light_color;
    bufptr[button_width - 1] = button_light_color;
    if (y < 2) {
      memset32(bufptr, button_dark_color, button_width - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr, button_light_color, button_width);
      continue;
    }
    bufptr[0] = button_dark_color;
    bufptr[1] = button_dark_color;
  }
  ui_redraw_rect(button_width * hovered_app + icon_h_padding, 0, button_width, button_height);
}

void handle_mouse_unclick()
{
  if (hovered_app == -1)
    return;

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr = ui_buf + y * window_w + button_width * hovered_app + icon_h_padding;
    if (y < 2) {
      memset32(bufptr, background_color, button_width);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr, background_color, button_width);
      continue;
    }
    bufptr[0] = background_color;
    bufptr[1] = background_color;
    bufptr[button_width - 2] = background_color;
    bufptr[button_width - 1] = background_color;
  }

  memset32(ui_buf + button_height * window_w, background_color, text_height * window_w);

  ui_redraw_rect(0, 0, window_w, window_h);
  apps[hovered_app].handler(&apps[hovered_app]);
  hovered_app = -1;
}

int main(int argc, char *argv[])
{
  priority(1);

  window_w = num_apps * button_width + 2 * icon_h_padding;
  window_h = button_height + text_height;

  ui_buf = malloc(window_w * window_h * sizeof(uint32_t));
  if (ui_buf == NULL)
    return 1;

  memset32(ui_buf, background_color, window_w * window_h);
  for (unsigned i = 0; i < num_apps; ++i)
    draw_app_icon(i);

  int32_t err = ui_acquire_window(ui_buf, "apps", window_w, window_h);
  if (err < 0)
    return 1;

  ui_event_t ev;
  err = ui_next_event(&ev);
  if (err < 0 || ev.type != UI_EVENT_WAKE)
    return 1;

  ui_redraw_rect(0, 0, window_w, window_h);
  ui_enable_mouse_move_events();

  while (1) {
    err = ui_next_event(&ev);
    if (err < 0)
      return 1;
    switch (ev.type) {
      case UI_EVENT_MOUSE_CLICK:
        handle_mouse_click(ev.x, ev.y);
        break;
      case UI_EVENT_MOUSE_UNCLICK:
        handle_mouse_unclick();
        break;
      case UI_EVENT_MOUSE_MOVE:
        handle_mouse_move(ev.x, ev.y);
        break;
      default:;
    }
  }

  return 0;
}
