
// xed.c
//
// eXtended EDitor.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <ui.h>
#include <mako.h>
#include "SDL_picofont.h"
#include "scancode.h"

#define FOOTER_LEN          99
#define LINE_LEN_TABLE_SIZE 40

static const uint32_t BG_COLOR       = 0xffffff;
static const uint32_t TEXT_COLOR     = 0;
static const uint32_t INACTIVE_COLOR = 0xb0b0b0;
static const uint32_t CURSOR_COLOR   = 0x190081;
static const uint32_t PATH_HEIGHT    = 20;
static const uint32_t FOOTER_HEIGHT  = 20;
static const uint32_t LINE_HEIGHT    = (FNT_FONTHEIGHT * FNT_FONTVSCALE)
  + FNT_FONTVPADDING;

static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;

static char *file_path = NULL;
static char *file_buffer = NULL;
static uint32_t line_lengths[LINE_LEN_TABLE_SIZE];
static uint32_t file_buffer_len = 0;
static char *buffer_dirty = 0;
static uint32_t top_idx = 0;
static uint32_t cursor_idx = 0;
static uint32_t buffer_idx = 0;

typedef enum {
  CS_READ,
  CS_EDIT,
  CS_SELECT
} command_state_t;

static command_state_t cs;
static char footer_text[FOOTER_LEN];
static uint32_t footer_idx = 0;

__attribute__((always_inline))
static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{ for (uint32_t i = 0; i < n; ++i) p[i] = b; }

static char *rstrchr(char *a, char *b, char c)
{
  for (; b > a && *b != c; --b);
  return b;
}

static uint32_t render_text(const char *text, uint32_t x, uint32_t y)
{
  uint32_t len = strlen(text);
  FNT_xy dim = FNT_Generate(text, len, 0, NULL);
  uint32_t w = dim.x;
  uint32_t h = dim.y;
  uint8_t *pixels = malloc(w * h);
  memset(pixels, 0, w * h);
  FNT_Generate(text, len, w, pixels);

  for (uint32_t i = 0; i < w && x + i < window_w; ++i)
    for (uint32_t j = 0; j < h && y + j < window_h; ++j)
      if (pixels[(j * w) + i])
        ui_buf[((y + j) * window_w) + x + i] = TEXT_COLOR;

  free(pixels);
  return h;
}

static void render_inactive()
{
  for (uint32_t i = 0; i < window_w; ++i)
    for (uint32_t j = 0; j < window_h; ++j)
      if (ui_buf[(j * window_w) + i] != BG_COLOR)
        ui_buf[(j * window_w) + i] = INACTIVE_COLOR;
}

static void render_path()
{
  char *path = NULL;
  size_t len = 0;
  if (file_path == NULL) {
    len = strlen("[No Name]");
    path = calloc(1, len + 2);
    strcpy(path, "[No Name]");
  } else {
    len = strlen(file_path);
    path = calloc(1, len + 2);
    strcpy(path, file_path);
    path[len] = buffer_dirty ? '*' : ' ';
  }
  fill_color(ui_buf, BG_COLOR, window_w * PATH_HEIGHT);
  render_text(path, 4, 4);
  uint32_t *line_row = ui_buf + (PATH_HEIGHT * window_w);
  fill_color(line_row, INACTIVE_COLOR, window_w);
}

static void render_footer()
{
  uint32_t *line_row = ui_buf + ((window_h - FOOTER_HEIGHT) * window_w);
  fill_color(line_row, BG_COLOR, window_w * FOOTER_HEIGHT);
  render_text(footer_text, 4, window_h - FOOTER_HEIGHT + 4);
  fill_color(line_row, INACTIVE_COLOR, window_w);
}

static void render_buffer()
{
  fill_color(
    ui_buf + (window_w * (PATH_HEIGHT + 1)),
    BG_COLOR,
    window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1)
    );

  if (file_buffer == NULL) return;
  if (top_idx >= file_buffer_len) return;
  char *p = file_buffer + top_idx;

  uint32_t len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t ht = window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16;
  uint32_t top = PATH_HEIGHT + 8;

  char *buf = calloc(1, len + 1);
  uint32_t nh = 0;
  uint32_t llidx = 0;
  for (
    uint32_t y = 0;
    y < ht && llidx < LINE_LEN_TABLE_SIZE && p < file_buffer + file_buffer_len;
    y += nh, ++llidx
    )
  {
    if (line_lengths[llidx] == 0) {
      nh = (uint32_t)(FNT_FONTHEIGHT * FNT_FONTVSCALE) + FNT_FONTVPADDING;
      ++p;
      continue;
    }
    size_t llen = line_lengths[llidx];
    strncpy(buf, p, llen);
    buf[llen] = '\0';
    nh = render_text(buf, 8, top + y);
    p += llen + (p[llen] == '\n');
  }
  free(buf);
}

static void update_line_lengths(uint32_t top_line, uint32_t fidx)
{
  memset(line_lengths, 0, sizeof(line_lengths));
  if (top_idx >= file_buffer_len) return;

  uint32_t len = (window_w - 16) / FNT_FONTWIDTH;
  for (
    uint32_t i = top_line;
    i < LINE_LEN_TABLE_SIZE && fidx < file_buffer_len;
    ++i
    )
  {
    char *p = file_buffer + fidx;
    if (*p == '\n') {
      line_lengths[i] = 0;
      ++fidx;
      continue;
    }

    uint32_t llen = len;
    char *nextnl = strchr(p, '\n');
    if (nextnl && nextnl - p < len) llen = nextnl - p;
    fidx += llen + (llen < len);
    line_lengths[i] = llen;
  }
}

static void update_cursor(uint32_t new_idx, uint8_t erase)
{
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  if (erase && cursor_idx / line_len < num_lines) {
    uint32_t x = ((cursor_idx % line_len) * FNT_FONTWIDTH) + 8;
    uint32_t cy = cursor_idx / line_len;
    uint32_t *pos = ui_buf
      + (((cy * LINE_HEIGHT) + 8 + PATH_HEIGHT) * window_w);
    for (uint32_t i = 0; i < LINE_HEIGHT; ++i) {
      for (uint32_t j = x; j < x + FNT_FONTWIDTH; ++j) {
        if (pos[j] == CURSOR_COLOR) pos[j] = BG_COLOR;
        else pos[j] = TEXT_COLOR;
      }
      pos += window_w;
    }
  }

  cursor_idx = new_idx;
  uint32_t x = ((cursor_idx % line_len) * FNT_FONTWIDTH) + 8;
  uint32_t cy = cursor_idx / line_len;
  uint32_t *pos = ui_buf
    + (((cy * LINE_HEIGHT) + 8 + PATH_HEIGHT) * window_w);
  for (uint32_t i = 0; i < LINE_HEIGHT; ++i) {
    for (uint32_t j = x; j < x + FNT_FONTWIDTH; ++j) {
      if (pos[j] == TEXT_COLOR) pos[j] = BG_COLOR;
      else pos[j] = CURSOR_COLOR;
    }
    pos += window_w;
  }
}

static void load_file()
{
  top_idx = 0;
  if (file_path == NULL) {
  fail:
    free(file_buffer);
    file_buffer = NULL;
    file_buffer_len = 0;
    return;
  }

  FILE *f = fopen(file_path, "rw+");
  if (f == NULL) goto fail;

  struct stat st;
  int32_t res = fstat(f->fd, &st);
  if (res) { fclose(f); goto fail; }

  free(file_buffer);
  file_buffer = malloc(st.st_size + 1);
  fread(file_buffer, st.st_size, 1, f);
  file_buffer[st.st_size] = '\0';
  file_buffer_len = st.st_size;

  fclose(f);
}

static uint32_t scroll_up()
{
  if (top_idx == 0) return cursor_idx;
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;

  // Scrolling {
  char *p = rstrchr(file_buffer, file_buffer + top_idx - 1, '\n');
  if (*p != '\n') return cursor_idx;
  uint32_t pi = p - file_buffer;
  char *pp = rstrchr(file_buffer, p - 1, '\n');
  if (pp < file_buffer) return cursor_idx;
  if (*pp == '\n') ++pp;
  uint32_t ppi = pp - file_buffer;

  uint32_t new_cursor_idx = cursor_idx;
  new_cursor_idx += (1 + ((pi - ppi) / line_len)) * line_len;
  top_idx = ppi;
  update_line_lengths(0, top_idx);
  // }

  // Moving {
  uint32_t fallback = new_cursor_idx;
  char *q = rstrchr(file_buffer, file_buffer + buffer_idx - 1, '\n');
  if (q < file_buffer) return fallback;
  if (*q != '\n') return fallback;
  uint32_t qi = q - file_buffer;
  if (qi < top_idx) return fallback;

  new_cursor_idx -= buffer_idx - qi;
  new_cursor_idx -= new_cursor_idx % line_len;
  new_cursor_idx += line_lengths[new_cursor_idx / line_len];
  buffer_idx = qi;
  // }

  return new_cursor_idx;
}

static uint32_t scroll_down()
{
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;

  // Scrolling {
  char *n = strchr(file_buffer + top_idx, '\n');
  if (n == NULL) return cursor_idx;
  uint32_t ni = n - file_buffer;
  if (ni + 1 >= file_buffer_len) return cursor_idx;

  uint32_t new_cursor_idx = cursor_idx;
  new_cursor_idx -= (1 + ((ni - top_idx) / line_len)) * line_len;
  top_idx = ni + 1;
  update_line_lengths(0, top_idx);
  // }

  // Moving {
  uint32_t fallback = new_cursor_idx;
  char *m = strchr(file_buffer + buffer_idx, '\n');
  if (m == NULL) return fallback;
  uint32_t mi = m - file_buffer;
  if (mi + 1 >= file_buffer_len) return fallback;

  new_cursor_idx += mi - buffer_idx;
  new_cursor_idx += line_len - (new_cursor_idx % line_len);

  if (new_cursor_idx / line_len >= num_lines) return fallback;
  buffer_idx = mi + 1;
  // }

  return new_cursor_idx;
}

static void update_footer_text()
{
  footer_idx = 0;
  char *str = NULL;
  switch (cs) {
  case CS_READ:
    str = "READ    [e]edit | [v]select | [s]save | [o]open | [q]quit";
    break;
  case CS_EDIT:
    str = "EDIT    [ESC]read";
    break;
  case CS_SELECT:
    str = "SELECT  [ESC]read | [c]copy | [k]kill | [p]paste";
    break;
  }
  size_t len = strlen(str);
  size_t min = len < FOOTER_LEN ? len : FOOTER_LEN;
  strncpy(footer_text, str, FOOTER_LEN);
  footer_idx = min;
}

static int32_t move_up()
{
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  char *p = rstrchr(file_buffer, file_buffer + buffer_idx - 1, '\n');
  if (p < file_buffer) return cursor_idx;
  if (*p != '\n') return cursor_idx;
  uint32_t pi = p - file_buffer;
  if (pi < top_idx) return -1;

  uint32_t new_cursor_idx = cursor_idx;
  new_cursor_idx -= buffer_idx - pi;
  new_cursor_idx -= new_cursor_idx % line_len;
  new_cursor_idx += line_lengths[new_cursor_idx / line_len];
  buffer_idx = pi;

  return new_cursor_idx;
}

static int32_t move_down()
{
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;

  char *n = strchr(file_buffer + buffer_idx, '\n');
  if (n == NULL) return cursor_idx;
  uint32_t ni = n - file_buffer;
  if (ni + 1 >= file_buffer_len) return cursor_idx;

  uint32_t new_cursor_idx = cursor_idx;
  new_cursor_idx += ni - buffer_idx;
  new_cursor_idx += line_len - (new_cursor_idx % line_len);

  if (new_cursor_idx / line_len >= num_lines) return -1;
  buffer_idx = ni + 1;

  return new_cursor_idx;
}

static void keyboard_handler(uint8_t code)
{
  static uint8_t lshift = 0;
  static uint8_t rshift = 0;
  static uint8_t capslock = 0;
  static uint8_t meta = 0;

  if (code & 0x80) {
    code &= 0x7F;
    switch (code) {
    case KB_SC_LSHIFT: lshift = 0; break;
    case KB_SC_RSHIFT: rshift = 0; break;
    case KB_SC_META:   meta = 0; break;
    }
    return;
  }

  switch (code) {
  case KB_SC_LSHIFT:   lshift = 1; return;
  case KB_SC_RSHIFT:   rshift = 1; return;
  case KB_SC_CAPSLOCK: capslock = !capslock; return;
  case KB_SC_META:     meta = 1; return;
  case KB_SC_TAB:
    if (meta) {
      meta = 0; lshift = 0; rshift = 0; capslock = 0;
      render_inactive();
      ui_swap_buffers((uint32_t)ui_buf);
      ui_yield();
    }
    return;
  }

  if (cs == CS_READ) {
    uint32_t num_lines =
      (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;
    uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
    uint32_t cursor_x = cursor_idx % line_len;
    uint32_t cursor_y = cursor_idx / line_len;
    uint32_t up = 0;
    uint32_t down = 0;
    uint8_t update = 1;
    switch (code) {
    case KB_SC_LEFT:
      if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }
      if (file_buffer[buffer_idx - 1] == '\n') { update = 0; break; }
      update_cursor(cursor_idx - 1, 1);
      --buffer_idx;
      break;
    case KB_SC_RIGHT:
      if (file_buffer == NULL || buffer_idx == file_buffer_len - 1) {
        update = 0; break;
      }
      if (file_buffer[buffer_idx] == '\n') { update = 0; break; }
      if (cursor_y == num_lines - 1 && cursor_x == line_len - 1) {
        update = 0; break;
      } else update_cursor(cursor_idx + 1, 1);
      ++buffer_idx;
      break;
    case KB_SC_UP:
      if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }
      up = move_up();
      if (up == -1) {
        up = scroll_up();
        render_buffer();
        update_cursor(up, 0);
        break;
      }
      update_cursor(up, 1);
      break;
    case KB_SC_DOWN:
      if (file_buffer == NULL) { update = 0; break; }
      down = move_down();
      if (down == -1) {
        down = scroll_down();
        render_buffer();
        update_cursor(down, 0);
        break;
      }
      update_cursor(down, 1);
      break;
    case KB_SC_Q:
      exit(0);
      break;
    default: update = 0;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }
}

static void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_KEYBOARD) {
    keyboard_handler(ev.code);
    return;
  }

  if (window_w != ev.width || window_h != ev.height) {
    if (ui_buf) {
      uint32_t oldsize = window_w * window_h * 4;
      pagefree((uint32_t)ui_buf, (oldsize / 0x1000) + 1);
    }
    uint32_t size = ev.width * ev.height * 4;
    ui_buf = (uint32_t *)pagealloc((size / 0x1000) + 1);
    window_w = ev.width;
    window_h = ev.height;
  }

  fill_color(ui_buf, BG_COLOR, window_w * window_h);
  load_file();
  update_line_lengths(0, top_idx);
  render_path();
  render_footer();
  render_buffer();
  update_cursor(0, 0);
  buffer_idx = top_idx;
  if (ev.is_active == 0) render_inactive();

  ui_swap_buffers((uint32_t)ui_buf);
}

int main(int argc, char *argv[])
{
  if (argc > 1) file_path = strdup(argv[1]);

  memset(footer_text, 0, sizeof(footer_text));
  memset(line_lengths, 0, sizeof(line_lengths));
  load_file();
  cs = CS_READ;
  update_footer_text();

  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();

  while (1) ui_wait();

  return 0;
}
