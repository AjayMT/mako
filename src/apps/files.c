
// files.c
//
// Filesystem browser.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "files_icons.h"
#include <dirent.h>
#include <mako.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ui.h>
#include <unistd.h>

static uint32_t *ui_buf = NULL;
static struct ui_scrollview view;

const uint32_t background_color = 0xffffff;
const uint32_t selected_background_color = 0xeeeeee;
const uint32_t text_color = 0;
const uint32_t item_height = 20;
const uint32_t num_buttons = 3;
const uint32_t button_width = 50;

enum item_type
{
  ITEM_FILE,
  ITEM_DIRECTORY,
  ITEM_APP,
  ITEM_UNKNOWN,
};

struct item
{
  enum item_type type;
  struct dirent dirent;
};

#define BUFSIZE 256

struct item items[BUFSIZE];
static uint32_t num_items = 0;
static int32_t selected_item = -1;

enum item_type get_item_type(const char *cwd, const char *name)
{
  struct stat st;
  int32_t err = stat(name, &st);
  if (err)
    return ITEM_UNKNOWN;

  if (st.st_dev & 1) {
    if (strcmp(cwd, getenv("APPS_PATH")) == 0 || strcmp(cwd, getenv("PATH")) == 0)
      return ITEM_APP;

    return ITEM_FILE;
  } else if (st.st_dev & 2)
    return ITEM_DIRECTORY;

  return ITEM_UNKNOWN;
}

void load_items()
{
  char cur_path[BUFSIZE];
  memset(cur_path, 0, BUFSIZE);
  getcwd(cur_path, BUFSIZE);

  DIR *d = opendir(cur_path);
  if (d == NULL)
    return;

  uint32_t item_idx = 0;
  struct dirent *ent = readdir(d);
  for (; ent != NULL && item_idx < BUFSIZE; free(ent), ent = readdir(d), ++item_idx) {
    items[item_idx].type = get_item_type(cur_path, ent->d_name);
    items[item_idx].dirent = *ent;
  }

  num_items = item_idx;
  closedir(d);
}

void render_item(uint32_t idx)
{
  const uint32_t *icon = NULL;
  switch (items[idx].type) {
    case ITEM_FILE:
      icon = FILE_ICON_PIXELS;
      break;
    case ITEM_DIRECTORY:
      icon = DIRECTORY_ICON_PIXELS;
      break;
    case ITEM_APP:
      icon = APP_ICON_PIXELS;
      break;
    case ITEM_UNKNOWN:
      icon = UNKNOWN_ICON_PIXELS;
      break;
  }

  if (!ui_scrollview_grow(&view, view.content_w, item_height * (idx + 2), 0))
    return;

  for (uint32_t y = 0; y < ICON_HEIGHT; ++y) {
    uint32_t *bufptr = view.content_buf + (item_height * idx + 2 + y) * view.content_w;
    const uint32_t *iconptr = icon + y * ICON_WIDTH;
    for (uint32_t x = 0; x < ICON_WIDTH; ++x)
      bufptr[2 + x] = ui_blend_alpha(bufptr[2 + x], iconptr[x]);
  }

  ui_render_text(view.content_buf + (item_height * idx + 4) * view.content_w + ICON_WIDTH + 8,
                 view.content_w,
                 items[idx].dirent.d_name,
                 strlen(items[idx].dirent.d_name),
                 UI_FONT_LUCIDA_GRANDE,
                 text_color);
}

void render_buttons()
{
  uint32_t *bufptr = ui_buf + view.window_h * view.window_w;
  memset32(bufptr, 0xeeeeff, item_height * view.window_w);

  char cur_path[BUFSIZE];
  memset(cur_path, 0, BUFSIZE);
  getcwd(cur_path, BUFSIZE);

  ui_render_text(bufptr + 2 * view.window_w + 8,
                 view.window_w,
                 cur_path,
                 strlen(cur_path),
                 UI_FONT_LUCIDA_GRANDE,
                 text_color);

  ui_redraw_rect(0, view.window_h, view.window_w, item_height);
}

void change_dir()
{
  selected_item = -1;
  memset32(view.content_buf, background_color, view.content_w * view.content_h);
  load_items();
  for (uint32_t i = 0; i < num_items; ++i)
    render_item(i);
  ui_scrollview_redraw_rect(&view, 0, 0, view.content_w, view.content_h);
  render_buttons();
}

void set_item_select(uint32_t idx, bool selected)
{
  memset32(view.content_buf + item_height * idx * view.content_w,
           selected ? selected_background_color : background_color,
           item_height * view.content_w);
  render_item(idx);
}

void handle_mouse_click(int32_t x, int32_t y)
{
  if (x < 0 || y < 0 || x >= (int32_t)view.window_w || y >= (int32_t)view.window_h)
    return;

  if ((uint32_t)y >= view.window_h - item_height) {
    // FIXME implement buttons
    return;
  }

  uint32_t item_idx = (y + view.window_y) / item_height;
  if (item_idx >= num_items) {
    if (selected_item != -1) {
      set_item_select(selected_item, false);
      ui_scrollview_redraw_rect(&view, 0, selected_item * item_height, view.content_w, item_height);
      selected_item = -1;
    }
    return;
  }

  if (item_idx != (uint32_t)selected_item) {
    uint32_t min_idx = item_idx;
    uint32_t max_idx = item_idx;
    if (selected_item != -1) {
      if ((uint32_t)selected_item < min_idx)
        min_idx = selected_item;
      if ((uint32_t)selected_item > max_idx)
        max_idx = selected_item;
      set_item_select(selected_item, false);
    }
    set_item_select(item_idx, true);
    selected_item = item_idx;
    ui_scrollview_redraw_rect(
      &view, 0, min_idx * item_height, view.content_w, (max_idx - min_idx + 1) * item_height);
    return;
  }

  switch (items[item_idx].type) {
    case ITEM_DIRECTORY: {
      chdir(items[item_idx].dirent.d_name);
      change_dir();
      break;
    }
    case ITEM_APP: {
      if (fork() == 0) {
        char *args[] = { NULL };
        execve(items[item_idx].dirent.d_name, args, environ);
        exit(1);
      }
      break;
    }
    case ITEM_FILE: {
      if (fork() == 0) {
        char *args[] = { items[item_idx].dirent.d_name, NULL };
        char write_path[BUFSIZE];
        // FIXME replace xed with write
        snprintf(write_path, BUFSIZE, "%s/xed", getenv("APPS_PATH"));
        execve(write_path, args, environ);
        exit(1);
      }
      break;
    }
    default:;
  }
}

void handle_resize_request(uint32_t w, uint32_t h)
{
  if (h < 3 * item_height)
    h = 3 * item_height;
  w = view.window_w;

  uint32_t *new_ui_buf = malloc(w * h * sizeof(uint32_t));
  if (new_ui_buf == NULL)
    return;

  struct ui_scrollview old_view = view;
  if (!ui_scrollview_init(&view, new_ui_buf, w, h - item_height)) {
    free(new_ui_buf);
    view = old_view;
    return;
  }

  if (!ui_scrollview_grow(&view, old_view.content_w, (num_items + 2) * item_height, 0)) {
    free(new_ui_buf);
    free(view.content_buf);
    view = old_view;
    return;
  }

  memset32(view.content_buf, background_color, view.content_w * view.content_h);
  for (uint32_t y = 0; y < (num_items + 2) * item_height; ++y)
    memcpy32(view.content_buf + y * view.content_w,
             old_view.content_buf + y * old_view.content_w,
             old_view.content_w);

  int32_t err = ui_resize_window(new_ui_buf, w, h);
  if (err) {
    free(new_ui_buf);
    free(view.content_buf);
    view = old_view;
    return;
  }

  free(old_view.content_buf);
  free(ui_buf);
  ui_buf = new_ui_buf;

  ui_scrollview_redraw_rect(&view, 0, 0, view.content_w, view.content_h);
  render_buttons();
}

int main(int argc, char *argv[])
{
  priority(1);

  if (argc > 1)
    chdir(argv[1]);

  const uint32_t window_w = 300;
  const uint32_t window_h = 280;
  ui_buf = malloc(window_w * window_h * sizeof(uint32_t));
  if (ui_buf == NULL)
    return 1;

  int32_t err = ui_acquire_window(ui_buf, "files", window_w, window_h);
  if (err < 0)
    return 1;

  ui_event_t ev;
  err = ui_next_event(&ev);
  if (err < 0 || ev.type != UI_EVENT_WAKE)
    return 1;

  if (!ui_scrollview_init(&view, ui_buf, ev.width, ev.height - item_height))
    return 1;

  render_buttons();
  change_dir();

  while (1) {
    err = ui_next_event(&ev);
    if (err < 0)
      return 1;
    switch (ev.type) {
      case UI_EVENT_MOUSE_CLICK:
        handle_mouse_click(ev.x, ev.y);
        break;
      case UI_EVENT_MOUSE_SCROLL:
        ui_scrollview_scroll(&view, ev.hscroll * 10, ev.vscroll * 10);
        break;
      case UI_EVENT_RESIZE_REQUEST:
        handle_resize_request(ev.width, ev.height);
        break;
      default:;
    }
  }

  return 0;
}
