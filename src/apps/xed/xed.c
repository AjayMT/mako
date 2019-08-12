
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
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <ui.h>
#include <mako.h>
#include "SDL_picofont.h"
#include "scancode.h"

#define FOOTER_LEN          99
#define LINE_LEN_TABLE_SIZE 40

static const uint32_t BG_COLOR        = 0xffffff;
static const uint32_t TEXT_COLOR      = 0;
static const uint32_t INACTIVE_COLOR  = 0xb0b0b0;
static const uint32_t CURSOR_COLOR    = 0x190081;
static const uint32_t SELECTION_COLOR = 0xb0d5ff;
static const uint32_t PATH_HEIGHT     = 20;
static const uint32_t FOOTER_HEIGHT   = 20;
static const uint32_t LINE_HEIGHT     = (FNT_FONTHEIGHT * FNT_FONTVSCALE)
  + FNT_FONTVPADDING;
static const uint32_t DEFAULT_BUFFER_CAPACITY = 512;

static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;

static char *file_path = NULL;
static char *file_buffer = NULL;
static char *paste_buffer = NULL;
static uint32_t line_lengths[LINE_LEN_TABLE_SIZE];
static uint32_t file_buffer_len = 0;
static uint32_t file_buffer_capacity = 0;
static uint32_t paste_buffer_len = 0;
static uint8_t buffer_dirty = 0;
static uint32_t top_idx = 0;
static uint32_t cursor_idx = 0;
static uint32_t buffer_idx = 0;
static uint32_t selection_top_idx = 0;
static uint32_t selection_cursor_idx = 0;
static uint32_t selection_buffer_idx = 0;
static int32_t selection_line_delta = 0;

typedef enum {
  CS_NORMAL,
  CS_EDIT,
  CS_SELECT,
  CS_CONFIRM_SAVE,
  CS_SAVE_PATH,
  CS_OPEN_PATH
} command_state_t;

static command_state_t cs;
static char footer_text[FOOTER_LEN];
static char footer_field[FOOTER_LEN];

__attribute__((always_inline))
static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{ for (uint32_t i = 0; i < n; ++i) p[i] = b; }

__attribute__((always_inline))
static inline int32_t round(double d)
{ return (int32_t)(fabs(d) + 0.5) * (d > 0 ? 1 : -1); }

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

static void clear_selection(uint32_t delta, uint32_t old, uint32_t new)
{
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;

  old -= delta * line_len;
  if (old < 0 || old >= num_lines * line_len)
    old = delta > 0 ? 0 : (num_lines * line_len) - 1;
  if (old == new) return;

  uint32_t lower = new;
  if (old < lower) lower = old;
  uint32_t higher = new;
  if (higher < old) higher = old;
  uint32_t ly = lower / line_len;
  uint32_t hy = higher / line_len;

  uint32_t *row = ui_buf
    + (window_w * (PATH_HEIGHT + 8 + ly * LINE_HEIGHT));

  uint32_t span = 0;
  for (uint32_t i = lower; i < higher; i += span) {
    uint32_t ix = i % line_len;
    span = higher - i;
    if (span > line_len - ix) span = line_len - ix;
    uint32_t xstart = 8 + (ix * FNT_FONTWIDTH);
    uint32_t xend = 8 + ((ix + span) * FNT_FONTWIDTH);
    for (uint32_t x = xstart; x < xend; ++x)
      for (uint32_t y = 0; y < LINE_HEIGHT; ++y)
        if (row[(y * window_w) + x] == SELECTION_COLOR)
          row[(y * window_w) + x] = BG_COLOR;
    row += window_w * LINE_HEIGHT;
  }
}

static void render_selection(int32_t delta)
{
  if (selection_buffer_idx == buffer_idx) return;

  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;

  uint32_t sci = selection_cursor_idx - (delta * line_len);
  if (sci < 0 || sci >= num_lines * line_len)
    sci = delta > 0 ? 0 : (num_lines * line_len) - 1;
  if (sci == cursor_idx) return;

  uint32_t lower = cursor_idx;
  if (sci < lower) lower = sci;
  uint32_t higher = cursor_idx;
  if (higher < sci) higher = sci;
  uint32_t ly = lower / line_len;

  uint32_t *row = ui_buf
    + (window_w * (PATH_HEIGHT + 8 + (ly * LINE_HEIGHT)));
  uint32_t span = 0;
  for (uint32_t i = lower; i < higher; i += span) {
    uint32_t ix = i % line_len;
    span = higher - i;
    if (span > line_len - ix) span = line_len - ix;
    uint32_t xstart = 8 + (ix * FNT_FONTWIDTH);
    uint32_t xend = 8 + ((ix + span) * FNT_FONTWIDTH);
    for (uint32_t x = xstart; x < xend; ++x)
      for (uint32_t y = 0; y < LINE_HEIGHT; ++y)
        if (row[(y * window_w) + x] == BG_COLOR)
          row[(y * window_w) + x] = SELECTION_COLOR;
    row += window_w * LINE_HEIGHT;
  }
}

static void render_buffer(uint32_t line_idx, uint32_t char_idx)
{
  uint32_t *top_row =
    ui_buf + (window_w * (PATH_HEIGHT + 8 + line_idx * LINE_HEIGHT));
  uint32_t fill_size =
    window_w * (
      window_h - PATH_HEIGHT - FOOTER_HEIGHT - 8 - (line_idx * LINE_HEIGHT)
      );
  if (line_idx == 0) {
    top_row = ui_buf + (window_w * (PATH_HEIGHT + 1));
    fill_size = window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1);
  }
  fill_color(top_row, BG_COLOR, fill_size);

  if (file_buffer == NULL) return;
  if (char_idx >= file_buffer_len) return;
  char *p = file_buffer + char_idx;

  uint32_t len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t ht = window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16;
  uint32_t top = PATH_HEIGHT + 8;

  char *buf = calloc(1, len + 1);
  uint32_t nh = 0;
  uint32_t llidx = line_idx;
  uint32_t y = LINE_HEIGHT * line_idx;
  for (
    ;
    y < ht && llidx < LINE_LEN_TABLE_SIZE && p < file_buffer + file_buffer_len;
    y += nh, ++llidx
    )
  {
    if (line_lengths[llidx] == 0) {
      nh = LINE_HEIGHT;
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
  memset(line_lengths + top_line, 0, 4 * (LINE_LEN_TABLE_SIZE - top_line));
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
    uint8_t nl = 0;
    if (nextnl && nextnl - p <= len) {
      nl = 1;
      llen = nextnl - p;
    }
    fidx += llen + nl;
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

static size_t save_file()
{
  FILE *f = fopen(file_path, "rw+");
  if (f == NULL) return 0;

  struct stat st;
  int32_t res = fstat(f->fd, &st);
  if (res || (st.st_dev & 1) == 0) { fclose(f); return 0; }

  size_t w = fwrite(file_buffer, file_buffer_len, 1, f);
  fclose(f);
  return w;
}

static uint8_t check_path(const char *path)
{
  struct stat st;
  int32_t res = stat(path, &st);
  if (res) return 0;
  if (st.st_dev & 2) return 2;
  return 1;
}

static void load_file()
{
  if (file_path == NULL) {
  fail:
    top_idx = 0;
    free(file_buffer);
    file_buffer = NULL;
    file_buffer_len = 0;
    return;
  }

  FILE *f = fopen(file_path, "rw+");
  if (f == NULL) goto fail;

  struct stat st;
  int32_t res = fstat(f->fd, &st);
  if (res || (st.st_dev & 1) == 0) { fclose(f); goto fail; }

  free(file_buffer);
  file_buffer = malloc(st.st_size + 1);
  fread(file_buffer, st.st_size, 1, f);
  file_buffer[st.st_size] = '\0';
  file_buffer_len = st.st_size;
  if (top_idx >= file_buffer_len)
    top_idx = 0;

  fclose(f);
}

__attribute__((always_inline))
static inline void backspace_scroll_up()
{
  char *p = rstrchr(file_buffer, file_buffer + top_idx, '\n');
  if (*p == '\n') ++p;
  uint32_t pi = p - file_buffer;
  top_idx = pi;
}

__attribute__((always_inline))
static inline uint32_t newline_scroll_down()
{
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  char *n = strchr(file_buffer + top_idx, '\n');
  uint32_t ni = n - file_buffer;
  uint32_t lines = (ni - top_idx) / line_len;
  top_idx = ni + 1;
  return ((cursor_idx / line_len) - lines) * line_len;
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
  char *str = NULL;
  switch (cs) {
  case CS_NORMAL:
    str = "- :: [e]* | [v]+ | [p] | [s] | [o] | [q]";
    break;
  case CS_EDIT:
    str = "* :: [ESC]";
    break;
  case CS_SELECT:
    str = "+ :: [ESC] | [c] | [k]";
    break;
  case CS_CONFIRM_SAVE:
    str = "Save? ([ESC]cancel) [y/n]"; break;
  case CS_SAVE_PATH:
  case CS_OPEN_PATH:
    str = "Path: ([ESC]cancel)"; break;
  }
  size_t len = strlen(str);
  size_t min = len < FOOTER_LEN ? len : FOOTER_LEN;
  strncpy(footer_text, str, FOOTER_LEN);
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
      return;
    }
  }

  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;
  uint32_t line_len = (window_w - 16) / FNT_FONTWIDTH;
  uint32_t cursor_x = cursor_idx % line_len;
  uint32_t cursor_y = cursor_idx / line_len;
  uint32_t up = 0;
  uint32_t down = 0;
  uint8_t update = 1;
  uint8_t moved = 1;

#define MOVEMENT                                                        \
  switch (code) {                                                       \
  case KB_SC_LEFT:                                                      \
    if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }  \
    if (file_buffer[buffer_idx - 1] == '\n') { update = 0; break; }     \
    update_cursor(cursor_idx - 1, 1);                                   \
    --buffer_idx;                                                       \
    break;                                                              \
  case KB_SC_RIGHT:                                                     \
    if (file_buffer == NULL || buffer_idx == file_buffer_len) {         \
      update = 0; break;                                                \
    }                                                                   \
    if (file_buffer[buffer_idx] == '\n') { update = 0; break; }         \
    if (cursor_y == num_lines - 1 && cursor_x == line_len - 1) {        \
      update = 0; break;                                                \
    } else update_cursor(cursor_idx + 1, 1);                            \
    ++buffer_idx;                                                       \
    break;                                                              \
  case KB_SC_UP:                                                        \
    if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }  \
    up = move_up();                                                     \
    if (up == -1) {                                                     \
      up = scroll_up();                                                 \
      render_buffer(0, top_idx);                                        \
      update_cursor(up, 0);                                             \
      break;                                                            \
    }                                                                   \
    update_cursor(up, 1);                                               \
    break;                                                              \
  case KB_SC_DOWN:                                                      \
    if (file_buffer == NULL) { update = 0; break; }                     \
    down = move_down();                                                 \
    if (down == -1) {                                                   \
      down = scroll_down();                                             \
      render_buffer(0, top_idx);                                        \
      update_cursor(down, 0);                                           \
      break;                                                            \
    }                                                                   \
    update_cursor(down, 1);                                             \
    break;                                                              \
  default: moved = 0;                                                   \
  }                                                                     \

  if (cs == CS_NORMAL) {
    MOVEMENT;
    if (moved) {
      if (update) ui_swap_buffers((uint32_t)ui_buf);
      return;
    }
    switch (code) {
    case KB_SC_E:
      cs = CS_EDIT;
      update_footer_text();
      render_footer();
      break;
    case KB_SC_V:
      cs = CS_SELECT;
      selection_cursor_idx = cursor_idx;
      selection_buffer_idx = buffer_idx;
      selection_top_idx = top_idx;
      selection_line_delta = 0;
      update_footer_text();
      render_footer();
      break;
    case KB_SC_P:
      if (paste_buffer == NULL) { update = 0; break; }
      if (file_buffer == NULL || file_buffer_len == file_buffer_capacity) {
        file_buffer_capacity *= 2;
        if (file_buffer_capacity == 0)
          file_buffer_capacity = DEFAULT_BUFFER_CAPACITY;
        file_buffer = realloc(file_buffer, file_buffer_capacity);
      }
      memmove(
        file_buffer + buffer_idx + paste_buffer_len,
        file_buffer + buffer_idx,
        file_buffer_len - buffer_idx
        );
      file_buffer_len += paste_buffer_len;
      file_buffer[file_buffer_len] = '\0';
      memcpy(file_buffer + buffer_idx, paste_buffer, paste_buffer_len);
      update_line_lengths(cursor_y, buffer_idx - cursor_x);
      render_buffer(cursor_y, buffer_idx - cursor_x);
      update_cursor(cursor_idx, 0);
      break;
    case KB_SC_O:
      if (buffer_dirty) {
        cs = CS_CONFIRM_SAVE;
        update_footer_text();
        render_footer();
        break;
      }
      cs = CS_OPEN_PATH;
      update_footer_text();
      render_footer();
      break;
    case KB_SC_S:
      if (file_path == NULL) {
        cs = CS_SAVE_PATH;
        update_footer_text();
        render_footer();
        break;
      }
      if (save_file()) {
        buffer_dirty = 0;
        render_path();
      }
      break;
    case KB_SC_Q:
      if (buffer_dirty) {
        cs = CS_CONFIRM_SAVE;
        update_footer_text();
        render_footer();
        break;
      }
      if (window_w != SCREENWIDTH || window_h != SCREENHEIGHT)
        exit(0);
    default: update = 0;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_EDIT) {
    MOVEMENT;
    if (moved) {
      if (update) ui_swap_buffers((uint32_t)ui_buf);
      return;
    }
    char deleted = 0;
    char inserted = 0;
    uint32_t new_cursor_idx = 0;
    uint32_t old_line_len = 0;
    switch (code) {
    case KB_SC_ESC:
      cs = CS_NORMAL;
      update_footer_text();
      render_footer();
      break;
    case KB_SC_BS:
      if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }
      deleted = file_buffer[buffer_idx - 1];
      memmove(
        file_buffer + buffer_idx - 1,
        file_buffer + buffer_idx,
        file_buffer_len - buffer_idx
        );
      --file_buffer_len;
      --buffer_idx;
      file_buffer[file_buffer_len] = '\0';
      if (buffer_dirty == 0) {
        buffer_dirty = 1;
        render_path();
      }
      if (deleted == '\n') {
        if (cursor_y == 0) {
          backspace_scroll_up();
          update_line_lengths(0, top_idx);
          render_buffer(0, top_idx);
          update_cursor(buffer_idx - top_idx, 0);
          break;
        }
        old_line_len = line_lengths[cursor_y - 1];
        update_line_lengths(cursor_y - 1, buffer_idx - old_line_len);
        render_buffer(cursor_y - 1, buffer_idx - old_line_len);
        update_cursor(((cursor_y - 1) * line_len) + old_line_len, 0);
        break;
      }
      old_line_len = cursor_x - 1;
      if (cursor_x == 0) old_line_len = line_len - 1;
      update_line_lengths(
        (cursor_idx - 1) / line_len, buffer_idx - old_line_len
        );
      render_buffer(
        (cursor_idx - 1) / line_len, buffer_idx - old_line_len
        );
      update_cursor(cursor_idx - 1, 0);
      break;
    case KB_SC_TAB:
      if (file_buffer == NULL || file_buffer_len == file_buffer_capacity) {
        file_buffer_capacity *= 2;
        if (file_buffer_capacity == 0)
          file_buffer_capacity = DEFAULT_BUFFER_CAPACITY;
        file_buffer = realloc(file_buffer, file_buffer_capacity);
      }
      if (buffer_dirty == 0) {
        buffer_dirty = 1;
        render_path();
      }
      memmove(
        file_buffer + buffer_idx + 2,
        file_buffer + buffer_idx,
        file_buffer_len - buffer_idx
        );
      file_buffer[buffer_idx] = ' ';
      file_buffer[buffer_idx + 1] = ' ';
      buffer_idx += 2;
      file_buffer_len += 2;
      file_buffer[file_buffer_len] = '\0';
      if (cursor_y == num_lines - 1 && cursor_x >= line_len - 2) {
        new_cursor_idx = newline_scroll_down();
        new_cursor_idx += cursor_idx == line_len - 1;
        update_line_lengths(0, top_idx);
        render_buffer(0, top_idx);
        update_cursor(new_cursor_idx, 0);
        break;
      }
      update_line_lengths(cursor_y, buffer_idx - 2 - cursor_x);
      render_buffer(cursor_y, buffer_idx - 2 - cursor_x);
      update_cursor(cursor_idx + 2, 0);
      break;
    default:
      if (file_buffer == NULL || file_buffer_len == file_buffer_capacity) {
        file_buffer_capacity *= 2;
        if (file_buffer_capacity == 0)
          file_buffer_capacity = DEFAULT_BUFFER_CAPACITY;
        file_buffer = realloc(file_buffer, file_buffer_capacity);
      }
      inserted = scancode_to_ascii(code, lshift || rshift || capslock);
      if (inserted == 0) { update = 0; break; }
      memmove(
        file_buffer + buffer_idx + 1,
        file_buffer + buffer_idx,
        file_buffer_len - buffer_idx
        );
      file_buffer[buffer_idx] = inserted;
      ++buffer_idx;
      ++file_buffer_len;
      file_buffer[file_buffer_len] = '\0';
      if (buffer_dirty == 0) {
        buffer_dirty = 1;
        render_path();
      }
      if (
        cursor_y == num_lines - 1
        && (cursor_x == line_len - 1 || inserted == '\n')
        )
      {
        new_cursor_idx = newline_scroll_down();
        update_line_lengths(0, top_idx);
        render_buffer(0, top_idx);
        update_cursor(new_cursor_idx, 0);
        break;
      }
      update_line_lengths(cursor_y, buffer_idx - 1 - cursor_x);
      render_buffer(cursor_y, buffer_idx - 1 - cursor_x);
      if (inserted == '\n') update_cursor((cursor_y + 1) * line_len, 0);
      else update_cursor(cursor_idx + 1, 0);
      break;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_SELECT) {
    uint32_t old_cursor_idx = cursor_idx;
    uint32_t old_top_idx = top_idx;
    MOVEMENT;
    if (moved) {
      if (update) {
        int32_t delta = 0;
        if (top_idx != old_top_idx) {
          int32_t ti = top_idx;
          int32_t oti = old_top_idx;
          int32_t diff = ti - oti;
          diff += diff > 0 ? -1 : 1;
          delta = round((double)(diff) / line_len)
            + (top_idx < old_top_idx ? -1 : 1);
        }
        selection_line_delta += delta;
        clear_selection(delta, old_cursor_idx, cursor_idx);
        render_selection(selection_line_delta);
        ui_swap_buffers((uint32_t)ui_buf);
      }
      return;
    }
    uint32_t selection_len = buffer_idx - selection_buffer_idx;
    uint32_t lower = selection_buffer_idx;
    if (selection_buffer_idx > buffer_idx) {
      selection_len = selection_buffer_idx - buffer_idx;
      lower = buffer_idx;
    }
    uint32_t lci = cursor_idx;
    if (selection_cursor_idx < cursor_idx) lci = selection_cursor_idx;

#define CLEAR                                                   \
    cs = CS_NORMAL;                                             \
    clear_selection(                                            \
      selection_line_delta, selection_cursor_idx, cursor_idx    \
      );                                                        \
    update_footer_text();                                       \
    render_footer();

#define COPY                                                        \
    if (selection_len == 0) break;                                  \
    if (paste_buffer == NULL || paste_buffer_len < selection_len) { \
      paste_buffer_len = selection_len;                             \
      paste_buffer = realloc(paste_buffer, paste_buffer_len + 1);   \
    }                                                               \
    memcpy(paste_buffer, file_buffer + lower, selection_len);       \
    paste_buffer[selection_len] = '\0';                             \

    switch (code) {
    case KB_SC_C:
      COPY;
      CLEAR;
      break;
    case KB_SC_K:
      COPY;
      memmove(
        file_buffer + lower,
        file_buffer + lower + selection_len,
        file_buffer_len - lower - selection_len
        );
      file_buffer_len -= selection_len;
      if (buffer_idx > lower) buffer_idx -= selection_len;
      file_buffer[file_buffer_len] = '\0';
      if (lower < top_idx) {
        top_idx = selection_top_idx;
        buffer_idx = selection_buffer_idx;
        update_line_lengths(0, top_idx);
        render_buffer(0, top_idx);
        update_cursor(selection_cursor_idx, 0);
      } else {
        buffer_idx = lower;
        update_line_lengths(lci / line_len, lower - (lci % line_len));
        render_buffer(lci / line_len, lower - (lci % line_len));
        update_cursor(lci, 0);
      }
      CLEAR;
      break;
    case KB_SC_ESC:
      CLEAR;
      break;
    default: update = 0;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_CONFIRM_SAVE) {
    switch (code) {
    case KB_SC_Y:
      if (file_path == NULL) {
        cs = CS_SAVE_PATH;
        update_footer_text();
        render_footer();
        break;
      }
      if (save_file()) {
        buffer_dirty = 0;
        render_path();
        cs = CS_NORMAL;
        update_footer_text();
        render_footer();
      }
      break;
    case KB_SC_N:
      load_file();
      buffer_dirty = 0;
      buffer_idx = top_idx;
      render_path();
      update_line_lengths(0, 0);
      render_buffer(0, 0);
      update_cursor(0, 0);
      cs = CS_NORMAL;
      update_footer_text();
      render_footer();
      break;
    case KB_SC_ESC:
      cs = CS_NORMAL;
      update_footer_text();
      render_footer();
      break;
    default: update = 0;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  size_t field_len = strlen(footer_field);
  char field_char = 0;

#define FIELD_INPUT(action) {                                           \
    switch (code) {                                                     \
    case KB_SC_ESC:                                                     \
      memset(footer_field, 0, sizeof(footer_field));                    \
      cs = CS_NORMAL;                                                   \
      update_footer_text();                                             \
      render_footer();                                                  \
      break;                                                            \
    case KB_SC_BS:                                                      \
      if (field_len == 0) { update = 0; break; }                        \
      footer_field[field_len - 1] = 0;                                  \
      update_footer_text();                                             \
      strcat(footer_text, footer_field);                                \
      render_footer();                                                  \
      break;                                                            \
    case KB_SC_ENTER:                                                   \
      action;                                                           \
      break;                                                            \
    default:                                                            \
      field_char = scancode_to_ascii(code, lshift || rshift || capslock); \
      if (field_char == 0 || field_len == FOOTER_LEN) {                 \
        update = 0; break;                                              \
      }                                                                 \
      footer_field[field_len] = field_char;                             \
      footer_field[field_len + 1] = 0;                                  \
      update_footer_text();                                             \
      strcat(footer_text, footer_field);                                \
      render_footer();                                                  \
      break;                                                            \
    }                                                                   \
  }                                                                     \

  if (cs == CS_OPEN_PATH) {
    FIELD_INPUT({
        uint8_t type = check_path(footer_field);
        if (type == 0) { update = 0; break; }
        if (type == 1) {
          free(file_path);
          file_path = strdup(footer_field);
        } else {
          ui_split(UI_SPLIT_LEFT);
          if (fork() == 0) {
            char *args[3];
            args[0] = "dex"; args[1] = footer_field; args[2] = NULL;
            execve("/apps/dex", args, environ);
          }
        }
        cs = CS_NORMAL;
        update_footer_text();
        render_footer();
        memset(footer_field, 0, sizeof(footer_field));
        load_file(); buffer_dirty = 0; buffer_idx = top_idx;
        render_path();
        update_line_lengths(0, top_idx);
        render_buffer(0, top_idx);
        update_cursor(0, 0);
      });
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_SAVE_PATH) {
    FIELD_INPUT({
        free(file_path);
        file_path = strdup(footer_field);
        if (save_file()) {
          buffer_dirty = 0;
          render_path();
          cs = CS_NORMAL;
          update_footer_text();
          render_footer();
        }
      });
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
  if (cs == CS_SELECT) cs = CS_NORMAL;
  load_file(); buffer_dirty = 0;
  update_line_lengths(0, top_idx); buffer_idx = top_idx;
  render_path();
  render_footer();
  render_buffer(0, top_idx);
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
  memset(footer_field, 0, sizeof(footer_field));
  load_file(); buffer_dirty = 0;
  cs = CS_NORMAL;
  update_footer_text();

  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();

  while (1) ui_wait();

  return 0;
}
