
// files.c
//
// Filesystem browser.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "../common/scancode.h"
#include "files_icons.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
const uint32_t selected_background_color = 0x2a62d9;
const uint32_t toolbar_background_color = 0xd3ccbd;
const uint32_t button_light_color = 0xe9e6de;
const uint32_t button_dark_color = 0x9f8e6d;
const uint32_t text_color = 0;
const uint32_t item_height = 20;
const uint32_t button_height = 24;
const uint32_t toolbar_height = item_height + button_height + 4;

enum item_type
{
  ITEM_UNKNOWN = 0,
  ITEM_FILE,
  ITEM_DIRECTORY,
  ITEM_APP,
};

struct item
{
  enum item_type type;
  struct dirent dirent;
};

#define BUFSIZE 256

static struct item items[BUFSIZE];
static uint32_t num_items = 0;
static int32_t selected_item = -1;
static char copy_path[BUFSIZE];
static char copy_basename[BUFSIZE];

struct button
{
  char *name;
  const uint32_t *icon;
  void (*func)();
  uint32_t x;
  uint32_t w;
};

void new_file_button_handler();
void new_folder_button_handler();
void copy_button_handler();
void paste_button_handler();
void rename_button_handler();
void trash_button_handler();

static struct button buttons[] = {
  { "New File", FILE_ICON_PIXELS, new_file_button_handler, 0, 0 },
  { "New Folder", DIRECTORY_ICON_PIXELS, new_folder_button_handler, 0, 0 },
  { "Copy", COPY_ICON_PIXELS, copy_button_handler, 0, 0 },
  { "Paste", PASTE_ICON_PIXELS, paste_button_handler, 0, 0 },
  { "Rename", RENAME_ICON_PIXELS, rename_button_handler, 0, 0 },
  { "Trash", TRASH_ICON_PIXELS, trash_button_handler, 0, 0 },
};
const unsigned num_buttons = 6;
int32_t clicked_button = -1;

const uint32_t dialog_box_width = 200;
const uint32_t dialog_box_height = 3 * button_height + 12;
const uint32_t dialog_box_button_width = 60;
const uint32_t dialog_box_button_y = dialog_box_height - 4 - button_height;
const uint32_t dialog_box_ok_button_x = (dialog_box_width - (2 * dialog_box_button_width + 4)) / 2;
const uint32_t dialog_box_cancel_button_x = dialog_box_ok_button_x + dialog_box_button_width + 4;
const uint32_t dialog_box_text_field_width = dialog_box_width - 12;
const uint32_t dialog_box_text_field_y = button_height + 4;
const uint32_t dialog_box_text_field_x = 6;

enum dialog_box_buttons
{
  BUTTON_OK = 0,
  BUTTON_CANCEL = 1,
};

static bool dialog_box_active = false;
static uint32_t *dialog_box_buffer = NULL;
static uint32_t dialog_box_x = 0;
static uint32_t dialog_box_y = 0;
static int32_t dialog_box_clicked_button = -1;
static bool (*dialog_box_validate_input)() = NULL;
static void (*dialog_box_input_handler)() = NULL;
static char dialog_box_text[BUFSIZE];
static uint32_t dialog_box_text_idx = 0;

enum item_type get_item_type(const char *cwd, const char *name)
{
  struct stat st;
  int32_t err = stat(name, &st);
  if (err)
    return ITEM_UNKNOWN;

  if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
    if (strcmp(cwd, getenv("APPS_PATH")) == 0)
      return ITEM_APP;

    return ITEM_FILE;
  } else if (S_ISDIR(st.st_mode))
    return ITEM_DIRECTORY;

  return ITEM_UNKNOWN;
}

void load_items()
{
  char cur_path[BUFSIZE];
  memset(cur_path, 0, BUFSIZE);
  getcwd(cur_path, BUFSIZE - 1);

  DIR *d = opendir(cur_path);
  if (d == NULL)
    return;

  memset(&items, 0, sizeof(items));
  uint32_t item_idx = 0;
  struct dirent *ent = readdir(d);
  for (; ent != NULL && item_idx < BUFSIZE; free(ent), ent = readdir(d), ++item_idx) {
    items[item_idx].type = get_item_type(cur_path, ent->d_name);
    items[item_idx].dirent = *ent;
  }

  num_items = item_idx;
  closedir(d);
}

void render_item(uint32_t idx, uint32_t color)
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

  if (item_height * (idx + 2) > view.content_h)
    if (!ui_scrollview_resize(&view, view.content_w, item_height * (idx + 2)))
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
                 UI_FONT_TWINLEAF,
                 color);
}

void render_toolbar()
{
  uint32_t *bufptr = ui_buf + view.window_h * view.window_w;
  memset32(bufptr, toolbar_background_color, toolbar_height * view.window_w);

  char cur_path[BUFSIZE];
  memset(cur_path, 0, BUFSIZE);
  getcwd(cur_path, BUFSIZE);

  ui_render_text(bufptr + 4 * view.window_w + 8,
                 view.window_w,
                 cur_path,
                 strlen(cur_path),
                 UI_FONT_TWINLEAF,
                 text_color);

  bufptr += item_height * view.window_w;
  uint32_t button_x = 8;
  for (unsigned i = 0; i < num_buttons; ++i) {
    buttons[i].x = button_x;

    for (uint32_t y = 0; y < ICON_HEIGHT; ++y) {
      uint32_t *bufp = bufptr + (4 + y) * view.window_w + button_x;
      const uint32_t *iconp = buttons[i].icon + y * ICON_WIDTH;
      for (uint32_t x = 0; x < ICON_WIDTH; ++x)
        bufp[4 + x] = ui_blend_alpha(bufp[4 + x], iconp[x]);
    }

    const size_t name_len = strlen(buttons[i].name);
    uint32_t w, h;
    ui_measure_text(&w, &h, buttons[i].name, name_len, UI_FONT_TWINLEAF);
    ui_render_text(bufptr + 6 * view.window_w + button_x + ICON_WIDTH + 6,
                   view.window_w,
                   buttons[i].name,
                   name_len,
                   UI_FONT_TWINLEAF,
                   text_color);

    const uint32_t button_width = ICON_WIDTH + w + 12;
    buttons[i].w = button_width;

    for (uint32_t y = 0; y < button_height; ++y) {
      uint32_t *bufp = bufptr + y * view.window_w + button_x;
      bufp[button_width - 2] = button_dark_color;
      bufp[button_width - 1] = button_dark_color;
      if (y < 2) {
        memset32(bufp, button_light_color, button_width - 2);
        continue;
      }
      if (y >= button_height - 2) {
        memset32(bufp, button_dark_color, button_width);
        continue;
      }
      bufp[0] = button_light_color;
      bufp[1] = button_light_color;
    }

    button_x += button_width + 4;
  }

  ui_redraw_rect(0, view.window_h, view.window_w, toolbar_height);
}

void set_item_select(uint32_t idx, bool selected)
{
  memset32(view.content_buf + item_height * idx * view.content_w,
           selected ? selected_background_color : background_color,
           item_height * view.content_w);
  render_item(idx, selected ? background_color : text_color);
}

void update_items()
{
  memset32(view.content_buf, background_color, view.content_w * view.content_h);
  load_items();
  ui_scrollview_resize(&view, view.content_w, item_height * (num_items + 2));
  for (uint32_t i = 0; i < num_items; ++i)
    render_item(i, text_color);
  if (selected_item != -1)
    set_item_select(selected_item, true);
  ui_scrollview_redraw_rect(&view, 0, 0, view.content_w, view.content_h);
  render_toolbar();
}

void dialog_box_close()
{
  dialog_box_active = false;
  memset(dialog_box_text, 0, sizeof(dialog_box_text));
  dialog_box_text_idx = 0;
  dialog_box_clicked_button = -1;
  dialog_box_validate_input = NULL;
  dialog_box_input_handler = NULL;
  for (uint32_t y = 0; y < dialog_box_height; ++y) {
    uint32_t *bufptr = ui_buf + (dialog_box_y + y) * view.window_w + dialog_box_x;
    memcpy32(bufptr, dialog_box_buffer + y * dialog_box_width, dialog_box_width);
  }
  update_items();
}

void dialog_box_keyboard(uint8_t code)
{
  static bool lshift = false;
  static bool rshift = false;

  if (code & 0x80) {
    code &= 0x7F;
    switch (code) {
      case KB_SC_LSHIFT:
        lshift = false;
        break;
      case KB_SC_RSHIFT:
        rshift = false;
        break;
    }
    return;
  }

  bool redraw_text_field = false;
  switch (code) {
    case KB_SC_LSHIFT:
      lshift = true;
      break;
    case KB_SC_RSHIFT:
      rshift = true;
      break;
    case KB_SC_ESC:
      dialog_box_close();
      break;
    case KB_SC_ENTER: {
      if (dialog_box_validate_input != NULL && dialog_box_validate_input()) {
        if (dialog_box_input_handler != NULL)
          dialog_box_input_handler();
        dialog_box_close();
      }
      break;
    }
    case KB_SC_BS: {
      if (dialog_box_text_idx == 0)
        break;
      --dialog_box_text_idx;
      dialog_box_text[dialog_box_text_idx] = 0;
      redraw_text_field = true;
      break;
    }
    default: {
      char c = scancode_to_ascii(code, lshift || rshift);
      if (c == 0)
        break;
      if (dialog_box_text_idx >= BUFSIZE - 1)
        break;
      dialog_box_text[dialog_box_text_idx] = c;
      ++dialog_box_text_idx;
      redraw_text_field = true;
    }
  }

  if (!redraw_text_field)
    return;

  uint32_t *bufptr = ui_buf + (dialog_box_y + dialog_box_text_field_y + 2) * view.window_w +
                     dialog_box_x + dialog_box_text_field_x + 2;
  for (uint32_t y = 0; y < button_height - 4; ++y)
    memset32(bufptr + y * view.window_w, background_color, dialog_box_text_field_width - 4);

  uint32_t w, h;
  ui_measure_text(&w, &h, dialog_box_text, dialog_box_text_idx, UI_FONT_TWINLEAF);
  ui_render_text(bufptr + 4 * view.window_w,
                 view.window_w,
                 dialog_box_text,
                 dialog_box_text_idx,
                 UI_FONT_TWINLEAF,
                 text_color);
  for (uint32_t y = 0; y < button_height - 6; ++y) {
    bufptr[(y + 1) * view.window_w + w] = text_color;
  }

  ui_redraw_rect(dialog_box_x + dialog_box_text_field_x,
                 dialog_box_y + dialog_box_text_field_y,
                 dialog_box_text_field_width,
                 button_height);
}

void dialog_box_unclick()
{
  if (dialog_box_clicked_button == -1)
    return;

  if (dialog_box_clicked_button == BUTTON_CANCEL) {
    dialog_box_close();
    return;
  }

  if (dialog_box_validate_input != NULL && dialog_box_validate_input()) {
    if (dialog_box_input_handler != NULL)
      dialog_box_input_handler();
    dialog_box_close();
    return;
  }

  dialog_box_clicked_button = -1;

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr = ui_buf + (dialog_box_y + dialog_box_button_y + y) * view.window_w +
                       dialog_box_x + dialog_box_ok_button_x;

    bufptr[dialog_box_button_width - 2] = button_dark_color;
    bufptr[dialog_box_button_width - 1] = button_dark_color;
    if (y < 2) {
      memset32(bufptr, button_light_color, dialog_box_button_width - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr, button_dark_color, dialog_box_button_width);
      continue;
    }
    bufptr[0] = button_light_color;
    bufptr[1] = button_light_color;
  }

  ui_redraw_rect(dialog_box_x + dialog_box_ok_button_x,
                 dialog_box_y + dialog_box_button_y,
                 dialog_box_button_width,
                 button_height);
}

void dialog_box_click(int32_t x, int32_t y)
{
  if ((uint32_t)x >= dialog_box_x + dialog_box_width ||
      (uint32_t)y >= dialog_box_y + dialog_box_height)
    return;

  if (dialog_box_clicked_button != -1) {
    dialog_box_unclick();
    return;
  }

  const uint32_t dbx = x - dialog_box_x;
  const uint32_t dby = y - dialog_box_y;
  if (dby >= dialog_box_button_y && dby <= dialog_box_button_y + button_height) {
    if (dbx >= dialog_box_ok_button_x && dbx < dialog_box_ok_button_x + dialog_box_button_width)
      dialog_box_clicked_button = BUTTON_OK;
    else if (dbx >= dialog_box_cancel_button_x &&
             dbx < dialog_box_cancel_button_x + dialog_box_button_width)
      dialog_box_clicked_button = BUTTON_CANCEL;
  }

  if (dialog_box_clicked_button == -1)
    return;

  const uint32_t bx =
    dialog_box_clicked_button == BUTTON_OK ? dialog_box_ok_button_x : dialog_box_cancel_button_x;
  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr =
      ui_buf + (dialog_box_y + dialog_box_button_y + y) * view.window_w + dialog_box_x + bx;

    bufptr[dialog_box_button_width - 2] = button_light_color;
    bufptr[dialog_box_button_width - 1] = button_light_color;
    if (y < 2) {
      memset32(bufptr, button_dark_color, dialog_box_button_width - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr, button_light_color, dialog_box_button_width);
      continue;
    }
    bufptr[0] = button_dark_color;
    bufptr[1] = button_dark_color;
  }

  ui_redraw_rect(
    dialog_box_x + bx, dialog_box_y + dialog_box_button_y, dialog_box_button_width, button_height);
}

void dialog_box(const char *prompt)
{
  dialog_box_active = true;
  memset(dialog_box_text, 0, sizeof(dialog_box_text));
  dialog_box_text_idx = 0;
  dialog_box_clicked_button = -1;

  for (uint32_t y = 0; y < dialog_box_height; ++y) {
    uint32_t *bufptr = dialog_box_buffer + y * dialog_box_width;
    memset32(bufptr, toolbar_background_color, dialog_box_width);

    bufptr[dialog_box_width - 2] = button_dark_color;
    bufptr[dialog_box_width - 1] = button_dark_color;
    if (y < 2) {
      memset32(bufptr, button_light_color, dialog_box_width - 2);
      continue;
    }
    if (y >= dialog_box_height - 2) {
      memset32(bufptr, button_dark_color, dialog_box_width);
      continue;
    }
    bufptr[0] = button_light_color;
    bufptr[1] = button_light_color;
  }

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr = dialog_box_buffer + (dialog_box_button_y + y) * dialog_box_width;

    bufptr[dialog_box_ok_button_x + dialog_box_button_width - 2] = button_dark_color;
    bufptr[dialog_box_ok_button_x + dialog_box_button_width - 1] = button_dark_color;
    bufptr[dialog_box_cancel_button_x + dialog_box_button_width - 2] = button_dark_color;
    bufptr[dialog_box_cancel_button_x + dialog_box_button_width - 1] = button_dark_color;
    if (y < 2) {
      memset32(bufptr + dialog_box_ok_button_x, button_light_color, dialog_box_button_width - 2);
      memset32(
        bufptr + dialog_box_cancel_button_x, button_light_color, dialog_box_button_width - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr + dialog_box_ok_button_x, button_dark_color, dialog_box_button_width);
      memset32(bufptr + dialog_box_cancel_button_x, button_dark_color, dialog_box_button_width);
      continue;
    }
    bufptr[dialog_box_ok_button_x] = button_light_color;
    bufptr[dialog_box_ok_button_x + 1] = button_light_color;
    bufptr[dialog_box_cancel_button_x] = button_light_color;
    bufptr[dialog_box_cancel_button_x + 1] = button_light_color;
  }

  uint32_t w, h;
  ui_measure_text(&w, &h, "OK", 2, UI_FONT_TWINLEAF);
  uint32_t *button_text_bufp = dialog_box_buffer + (dialog_box_button_y + 6) * dialog_box_width;
  ui_render_text(button_text_bufp + dialog_box_ok_button_x + ((dialog_box_button_width - w) / 2),
                 dialog_box_width,
                 "OK",
                 2,
                 UI_FONT_TWINLEAF,
                 text_color);

  ui_measure_text(&w, &h, "Cancel", 6, UI_FONT_TWINLEAF);
  ui_render_text(button_text_bufp + dialog_box_cancel_button_x +
                   ((dialog_box_button_width - w) / 2),
                 dialog_box_width,
                 "Cancel",
                 6,
                 UI_FONT_TWINLEAF,
                 text_color);

  ui_render_text(dialog_box_buffer + 8 * dialog_box_width + 8,
                 dialog_box_width,
                 prompt,
                 strlen(prompt),
                 UI_FONT_TWINLEAF,
                 text_color);

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufptr = dialog_box_buffer + (y + dialog_box_text_field_y) * dialog_box_width +
                       dialog_box_text_field_x;
    memset32(bufptr, background_color, dialog_box_text_field_width);

    bufptr[dialog_box_text_field_width - 2] = button_light_color;
    bufptr[dialog_box_text_field_width - 1] = button_light_color;
    if (y < 2) {
      memset32(bufptr, button_dark_color, dialog_box_text_field_width - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufptr, button_light_color, dialog_box_text_field_width);
      continue;
    }
    bufptr[0] = button_dark_color;
    bufptr[1] = button_dark_color;

    if (y >= 3 && y < button_height - 3)
      bufptr[2] = text_color;
  }

  dialog_box_x = (view.window_w - dialog_box_width) / 2;
  dialog_box_y = (view.window_h - dialog_box_height) / 2;
  uint32_t tmp[dialog_box_width];
  for (uint32_t y = 0; y < dialog_box_height; ++y) {
    uint32_t *bufptr = ui_buf + (dialog_box_y + y) * view.window_w + dialog_box_x;
    memcpy32(tmp, bufptr, dialog_box_width);
    memcpy32(bufptr, dialog_box_buffer + y * dialog_box_width, dialog_box_width);
    memcpy32(dialog_box_buffer + y * dialog_box_width, tmp, dialog_box_width);
  }

  ui_redraw_rect(dialog_box_x, dialog_box_y, dialog_box_width, dialog_box_height);
}

bool is_new_filename()
{
  struct stat st;
  int32_t err = stat(dialog_box_text, &st);
  if (err && errno == ENOENT)
    return true;
  return false;
}

void create_file()
{
  int32_t fd = open(dialog_box_text, O_CREAT, 0660);
  if (fd >= 0)
    close(fd);
}
void create_dir()
{
  mkdir(dialog_box_text, 0660);
}
void rename_item()
{
  rename(items[selected_item].dirent.d_name, dialog_box_text);
}

void copy_tree(const char *src, const char *dst)
{
  if (strcmp(src, dst) == 0)
    return;

  struct stat st;
  int32_t err = stat(src, &st);
  if (err)
    return;

  if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
    FILE *fsrc = fopen(src, "r");
    if (fsrc == NULL)
      return;
    FILE *fdst = fopen(dst, "w");
    if (fdst == NULL)
      return;

    char text[BUFSIZE];
    size_t copied = 0;
    while (copied < (size_t)st.st_size) {
      size_t nread = fread(text, 1, sizeof(text), fsrc);
      size_t nwritten = fwrite(text, 1, nread, fdst);
      while (nwritten < nread)
        nwritten += fwrite(text + nwritten, 1, nread - nwritten, fdst);
      copied += nread;
    }

    fclose(fsrc);
    fclose(fdst);
    return;
  }

  if (!S_ISDIR(st.st_mode))
    return;

  DIR *d = opendir(src);
  if (d == NULL)
    return;

  err = mkdir(dst, 0660);
  if (err)
    return;

  for (struct dirent *ent = readdir(d); ent != NULL; free(ent), ent = readdir(d)) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    char dstname[BUFSIZE];
    snprintf(dstname, BUFSIZE - 1, "%s/%s", dst, ent->d_name);
    char srcname[BUFSIZE];
    snprintf(srcname, BUFSIZE - 1, "%s/%s", src, ent->d_name);
    copy_tree(srcname, dstname);
  }

  closedir(d);
}

void unlink_tree(const char *path)
{
  struct stat st;
  int32_t err = stat(path, &st);
  if (err)
    return;

  if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
    unlink(path);
    return;
  }

  if (!S_ISDIR(st.st_mode))
    return;

  DIR *d = opendir(path);
  if (d == NULL)
    return;

  for (struct dirent *ent = readdir(d); ent != NULL; free(ent), ent = readdir(d)) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    char ent_path[BUFSIZE];
    snprintf(ent_path, BUFSIZE - 1, "%s/%s", path, ent->d_name);
    unlink_tree(ent_path);
  }

  closedir(d);
  rmdir(path);
}

void new_file_button_handler()
{
  dialog_box_validate_input = is_new_filename;
  dialog_box_input_handler = create_file;
  dialog_box("File name:");
}

void new_folder_button_handler()
{
  dialog_box_validate_input = is_new_filename;
  dialog_box_input_handler = create_dir;
  dialog_box("Folder name:");
}

void copy_button_handler()
{
  char path[BUFSIZE];
  memset(path, 0, BUFSIZE);
  int32_t err = resolve(path, items[selected_item].dirent.d_name, BUFSIZE - 1);
  if (err)
    return;
  strncpy(copy_basename, items[selected_item].dirent.d_name, BUFSIZE);
  memcpy(copy_path, path, BUFSIZE);
}

void paste_button_handler()
{
  char dst[BUFSIZE];
  strncpy(dst, copy_basename, BUFSIZE);

  struct stat st;
  int32_t err = stat(dst, &st);
  if (err && errno != ENOENT)
    return;

  if (!err)
    snprintf(dst, BUFSIZE, "%s copy", copy_basename);

  copy_tree(copy_path, dst);
  update_items();
}

void rename_button_handler()
{
  if (selected_item == -1)
    return;

  dialog_box_validate_input = is_new_filename;
  dialog_box_input_handler = rename_item;
  char prompt[BUFSIZE];
  snprintf(prompt, BUFSIZE, "Rename '%s' to:", items[selected_item].dirent.d_name);
  dialog_box(prompt);
}

void trash_button_handler()
{
  if (selected_item == -1)
    return;

  const char *item_name = items[selected_item].dirent.d_name;
  if (strcmp(item_name, ".") == 0 || strcmp(item_name, "..") == 0)
    return;

  char src[BUFSIZE];
  memset(src, 0, BUFSIZE);
  int32_t err = resolve(src, item_name, BUFSIZE - 1);
  if (err)
    return;

  if (strcmp(src, "/trash") == 0)
    return;

  char dst[BUFSIZE];
  snprintf(dst, BUFSIZE - 1, "/trash/%s", item_name);

  copy_tree(src, dst);
  unlink_tree(src);

  selected_item = -1;
  update_items();
}

void handle_mouse_unclick()
{
  if (clicked_button == -1)
    return;

  for (uint32_t y = 0; y < button_height; ++y) {
    uint32_t *bufp =
      ui_buf + (view.window_h + item_height + y) * view.window_w + buttons[clicked_button].x;
    bufp[buttons[clicked_button].w - 2] = button_dark_color;
    bufp[buttons[clicked_button].w - 1] = button_dark_color;
    if (y < 2) {
      memset32(bufp, button_light_color, buttons[clicked_button].w - 2);
      continue;
    }
    if (y >= button_height - 2) {
      memset32(bufp, button_dark_color, buttons[clicked_button].w);
      continue;
    }
    bufp[0] = button_light_color;
    bufp[1] = button_light_color;
  }

  ui_redraw_rect(buttons[clicked_button].x,
                 view.window_h + item_height,
                 buttons[clicked_button].w,
                 button_height);

  buttons[clicked_button].func();
  clicked_button = -1;
}

void handle_toolbar_click(uint32_t x, uint32_t y)
{
  if (clicked_button != -1) {
    handle_mouse_unclick();
    return;
  }

  if (y < view.window_h + item_height)
    return;

  for (unsigned i = 0; i < num_buttons; ++i) {
    if (x >= buttons[i].x && x < buttons[i].x + buttons[i].w) {
      for (uint32_t y = 0; y < button_height; ++y) {
        uint32_t *bufp = ui_buf + (view.window_h + item_height + y) * view.window_w + buttons[i].x;
        bufp[buttons[i].w - 2] = button_light_color;
        bufp[buttons[i].w - 1] = button_light_color;
        if (y < 2) {
          memset32(bufp, button_dark_color, buttons[i].w - 2);
          continue;
        }
        if (y >= button_height - 2) {
          memset32(bufp, button_light_color, buttons[i].w);
          continue;
        }
        bufp[0] = button_dark_color;
        bufp[1] = button_dark_color;
      }

      ui_redraw_rect(buttons[i].x, view.window_h + item_height, buttons[i].w, button_height);
      clicked_button = i;
      return;
    }
  }
}

void handle_mouse_click(int32_t x, int32_t y)
{
  if (x < 0 || y < 0 || (uint32_t)x >= view.window_w ||
      (uint32_t)y >= view.window_h + toolbar_height)
    return;

  if ((uint32_t)y >= view.window_h) {
    handle_toolbar_click(x, y);
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
      selected_item = -1;
      update_items();
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
        snprintf(write_path, BUFSIZE, "%s/write", getenv("APPS_PATH"));
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
  if (h < 3 * item_height + toolbar_height)
    h = 3 * item_height + toolbar_height;
  w = view.window_w;

  uint32_t *new_ui_buf = malloc(w * h * sizeof(uint32_t));
  if (new_ui_buf == NULL)
    return;

  struct ui_scrollview old_view = view;
  view.window_h = h - toolbar_height;
  view.window_buf = new_ui_buf;

  uint32_t max_h = (num_items + 2) * item_height;
  if (view.window_h > max_h)
    max_h = view.window_h;

  if (!ui_scrollview_resize(&view, view.content_w, max_h)) {
    free(new_ui_buf);
    view = old_view;
    return;
  }

  if (view.window_y + view.window_h > view.content_h)
    view.window_y = view.content_h - view.window_h;

  ui_scrollview_redraw_rect_buffered(&view, 0, 0, view.content_w, view.content_h);

  int32_t err = ui_resize_window(new_ui_buf, w, h);
  if (err) {
    free(new_ui_buf);
    view = old_view;
    return;
  }

  free(ui_buf);
  ui_buf = new_ui_buf;
  render_toolbar();
}

int main(int argc, char *argv[])
{
  priority(1);

  memset(items, 0, sizeof(items));
  memset(copy_path, 0, sizeof(copy_path));
  memset(copy_basename, 0, sizeof(copy_basename));

  if (argc > 1)
    chdir(argv[1]);

  dialog_box_buffer = malloc(dialog_box_width * dialog_box_height * sizeof(uint32_t));
  if (dialog_box_buffer == NULL)
    return 1;

  const uint32_t window_w = 450;
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

  if (!ui_scrollview_init(&view, ui_buf, ev.width, ev.height - toolbar_height, background_color))
    return 1;

  render_toolbar();
  update_items();

  while (1) {
    err = ui_next_event(&ev);
    if (err < 0)
      return 1;

    if (dialog_box_active) {
      switch (ev.type) {
        case UI_EVENT_MOUSE_CLICK:
          dialog_box_click(ev.x, ev.y);
          break;
        case UI_EVENT_MOUSE_UNCLICK:
          dialog_box_unclick();
          break;
        case UI_EVENT_KEYBOARD:
          dialog_box_keyboard(ev.code);
          break;
        default:;
      }
    } else {
      switch (ev.type) {
        case UI_EVENT_MOUSE_CLICK:
          handle_mouse_click(ev.x, ev.y);
          break;
        case UI_EVENT_MOUSE_UNCLICK:
          handle_mouse_unclick();
          break;
        case UI_EVENT_MOUSE_SCROLL:
          ui_scrollview_scroll(&view, ev.hscroll * 10, ev.vscroll * 10);
          break;
        case UI_EVENT_RESIZE_REQUEST:
          handle_resize_request(ev.width, ev.height);
          break;
        case UI_EVENT_WAKE:
          selected_item = -1;
          update_items();
          break;
        default:;
      }
    }
  }

  return 0;
}
