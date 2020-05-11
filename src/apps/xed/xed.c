
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
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/stat.h>
#include <ui.h>
#include <mako.h>
#include "text_render.h"
#include "scancode.h"

#define FOOTER_LEN    (SCREENWIDTH / FONTWIDTH)
#define LINE_HEIGHT   (FONTHEIGHT + FONTVPADDING)
#define MAX_NUM_LINES (SCREENHEIGHT / (FONTHEIGHT + FONTVPADDING))

static const uint32_t BG_COLOR        = 0xffffff;
static const uint32_t TEXT_COLOR      = 0;
static const uint32_t INACTIVE_COLOR  = 0xb0b0b0;
static const uint32_t CURSOR_COLOR    = 0x190081;
static const uint32_t SELECTION_COLOR = 0xb0d5ff;
static const uint32_t PATH_HEIGHT     = 24;
static const uint32_t FOOTER_HEIGHT   = 24;
static const uint32_t TOTAL_PADDING   = 16;

static const uint32_t DEFAULT_BUFFER_CAPACITY = 512;

// Window state
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;

// File buffer state
static char *file_path = NULL;
static char *file_buffer = NULL;
static uint32_t file_buffer_len = 0;
static uint32_t file_buffer_capacity = 0;

// Paste buffer state
static char *paste_buffer = NULL;
static uint32_t paste_buffer_len = 0;

// line array and position in the file
typedef struct line_s {
  uint32_t buffer_idx; // buffer index at the start of the line
  int32_t len; // length of the line
} line_t;
static line_t lines[MAX_NUM_LINES];
static uint32_t top_idx = 0;
static uint32_t cursor_idx = 0;
static uint32_t buffer_idx = 0;

// Directory path to open in dex
static char *dir_path = NULL;

// Buffer modification state
static uint8_t buffer_dirty = 0;

// selection state
static uint32_t selection_top_idx = 0;
static uint32_t selection_cursor_idx = 0;
static uint32_t selection_buffer_idx = 0;
static int32_t selection_line_delta = 0;

// 'Command' (footer) state
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

// search a string for `c` backwards from `b` to `a`
static char *rstrchr(char *a, char *b, char c)
{
  for (; b > a && *b != c; --b);
  return b;
}

// render text at x and y coordinates
static uint32_t render_text(const char *text, uint32_t x, uint32_t y)
{
  size_t len = strlen(text);
  size_t w, h;
  text_dimensions(text, len, &w, &h);

  uint8_t *pixels = malloc(w * h);
  memset(pixels, 0, w * h);
  text_render(text, len, w, h, pixels);

  uint32_t *p = ui_buf + (y * window_w) + x;
  for (uint32_t j = 0; j < h && y + j < window_h; ++j) {
    for (uint32_t i = 0; i < w && x + i < window_w; ++i)
      if (pixels[(j * w) + i])
        p[i] = TEXT_COLOR;
    p += window_w;
  }
  free(pixels);
  return h;
}

// gray out all UI elements when the window is inactive
__attribute__((always_inline))
static inline void render_inactive()
{
  for (uint32_t i = 0; i < window_w; ++i)
    for (uint32_t j = 0; j < window_h; ++j)
      if (ui_buf[(j * window_w) + i] != BG_COLOR)
        ui_buf[(j * window_w) + i] = INACTIVE_COLOR;
}

// render the path bar at the top of the window
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

// render the footer at the bottom of the window
static void render_footer()
{
  uint32_t *line_row = ui_buf + ((window_h - FOOTER_HEIGHT) * window_w);
  fill_color(line_row, BG_COLOR, window_w * FOOTER_HEIGHT);
  render_text(footer_text, 4, window_h - FOOTER_HEIGHT + 4);
  fill_color(line_row, INACTIVE_COLOR, window_w);
}

static void clear_selection(uint32_t delta, uint32_t old, uint32_t new)
{
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

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
    + (window_w * (PATH_HEIGHT + (TOTAL_PADDING / 2) + ly * LINE_HEIGHT));

  uint32_t span = 0;
  for (uint32_t i = lower; i < higher; i += span) {
    uint32_t ix = i % line_len;
    span = higher - i;
    if (span > line_len - ix) span = line_len - ix;
    uint32_t xstart = (TOTAL_PADDING / 2) + (ix * FONTWIDTH);
    uint32_t xend = (TOTAL_PADDING / 2) + ((ix + span) * FONTWIDTH);
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

  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

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
    + (window_w * (PATH_HEIGHT + (TOTAL_PADDING / 2) + (ly * LINE_HEIGHT)));
  uint32_t span = 0;
  for (uint32_t i = lower; i < higher; i += span) {
    uint32_t ix = i % line_len;
    span = higher - i;
    if (span > line_len - ix) span = line_len - ix;
    uint32_t xstart = (TOTAL_PADDING / 2) + (ix * FONTWIDTH);
    uint32_t xend = (TOTAL_PADDING / 2) + ((ix + span) * FONTWIDTH);
    for (uint32_t x = xstart; x < xend; ++x)
      for (uint32_t y = 0; y < LINE_HEIGHT; ++y)
        if (row[(y * window_w) + x] == BG_COLOR)
          row[(y * window_w) + x] = SELECTION_COLOR;
    row += window_w * LINE_HEIGHT;
  }
}

// Render all the text in the buffer, starting at line `line_idx`
static void render_buffer(uint32_t line_idx)
{
  if (file_buffer == NULL) return;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

  // filling the buffer area with the background color {
  uint32_t *top_row =
    ui_buf
    + (window_w * (PATH_HEIGHT + (TOTAL_PADDING / 2) + line_idx * LINE_HEIGHT));
  uint32_t fill_size =
    window_w * (
      window_h - PATH_HEIGHT - FOOTER_HEIGHT -
      (TOTAL_PADDING / 2) - (line_idx * LINE_HEIGHT)
      );
  if (line_idx == 0) {
    // exclude padding
    top_row = ui_buf + (window_w * (PATH_HEIGHT + 1));
    fill_size = window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1);
  }
  fill_color(top_row, BG_COLOR, fill_size);
  // }

  // rendering the lines {
  uint32_t top = PATH_HEIGHT + (TOTAL_PADDING / 2);
  for (uint32_t i = line_idx; i < num_lines && lines[i].len > 0; ++i) {
    uint32_t y = top + i * LINE_HEIGHT;
    char *line = strndup(file_buffer + lines[i].buffer_idx, lines[i].len);
    render_text(line, TOTAL_PADDING / 2, y);
    free(line);
  }
  // }
}

// Update the array of lines, starting at `line_idx` and index
// `buf_idx` of the buffer.
static void update_lines(uint32_t line_idx, uint32_t buf_idx)
{
  if (file_buffer == NULL) return;

  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  char *p = file_buffer + buf_idx;
  uint32_t i = line_idx;
  for (; i < num_lines && p < file_buffer + file_buffer_len; ++i) {
    lines[i].buffer_idx = p - file_buffer;
    char *next_nl = strchr(p, '\n');

    if (next_nl == NULL || next_nl - p > line_len) {
      // wrap text around without newline
      lines[i].len = line_len;
      p += line_len;
      continue;
    }

    lines[i].len = next_nl - p + 1;
    p = next_nl + 1;
  }
  if (i < num_lines) lines[i].len = -1;
  lines[num_lines].len = -1;
}

/* static void update_line_lengths(uint32_t top_line, uint32_t fidx) */
/* { */
/*   memset(line_lengths + top_line, 0, 4 * (LINE_LEN_TABLE_SIZE - top_line)); */
/*   if (top_idx >= file_buffer_len) return; */

/*   uint32_t len = (window_w - TOTAL_PADDING) / FONTWIDTH; */
/*   for ( */
/*     uint32_t i = top_line; */
/*     i < LINE_LEN_TABLE_SIZE && fidx < file_buffer_len; */
/*     ++i */
/*     ) */
/*   { */
/*     char *p = file_buffer + fidx; */
/*     if (*p == '\n') { */
/*       line_lengths[i] = 0; */
/*       ++fidx; */
/*       continue; */
/*     } */

/*     uint32_t llen = len; */
/*     char *nextnl = strchr(p, '\n'); */
/*     uint8_t nl = 0; */
/*     if (nextnl && nextnl - p <= len) { */
/*       nl = 1; */
/*       llen = nextnl - p; */
/*     } else if (nextnl == NULL && file_buffer_len - fidx < len) */
/*       llen = file_buffer_len - fidx; */
/*     fidx += llen + nl; */
/*     line_lengths[i] = llen; */
/*   } */
/* } */

// redraw the cursor at position `cursor_idx`.
// if `erase`, erase it from the old position `old_idx`
static void update_cursor(uint32_t old_idx, uint8_t erase)
{
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  if (erase && old_idx / line_len < num_lines) {
    // erase the cursor from previous position

    uint32_t cx = old_idx % line_len;
    uint32_t cy = old_idx / line_len;
    uint32_t x = (cx * FONTWIDTH) + (TOTAL_PADDING / 2);
    uint32_t y = (cy * LINE_HEIGHT) + (TOTAL_PADDING / 2) + PATH_HEIGHT;

    uint32_t *pos = ui_buf + (y * window_w);
    for (uint32_t i = 0; i < LINE_HEIGHT; ++i) {
      for (uint32_t j = x; j < x + FONTWIDTH; ++j) {
        if (pos[j] == CURSOR_COLOR) pos[j] = BG_COLOR;
        else pos[j] = TEXT_COLOR;
      }
      pos += window_w;
    }
  }

  uint32_t cx = cursor_idx % line_len;
  uint32_t cy = cursor_idx / line_len;
  uint32_t x = (cx * FONTWIDTH) + (TOTAL_PADDING / 2);
  uint32_t y = (cy * LINE_HEIGHT) + (TOTAL_PADDING / 2) + PATH_HEIGHT;

  uint32_t *pos = ui_buf + (y * window_w);
  for (uint32_t i = 0; i < LINE_HEIGHT; ++i) {
    for (uint32_t j = x; j < x + FONTWIDTH; ++j) {
      if (pos[j] == TEXT_COLOR) pos[j] = BG_COLOR;
      else pos[j] = CURSOR_COLOR;
    }
    pos += window_w;
  }
}

// Save the contents of file_buffer
static size_t save_file()
{
  int32_t fd = open(file_path, O_RDWR | O_TRUNC);
  if (fd == -1 && errno == ENOENT)
    fd = open(file_path, O_RDWR | O_CREAT, 0666);

  if (fd == -1) return 0;

  struct stat st;
  int32_t res = fstat(fd, &st);
  if (res || (st.st_dev & 1) == 0) { close(fd); return 0; }

  size_t w = write(fd, file_buffer, file_buffer_len);
  close(fd);
  return w;
}

// check if a path is a directory
// 1 -- file
// 2 -- dir
static uint8_t check_path(const char *path)
{
  struct stat st;
  int32_t res = stat(path, &st);
  if (res) return 0;
  if (st.st_dev & 2) return 2;
  return 1;
}

// Load the file at `file_path` into file_buffer
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
  file_buffer_capacity = st.st_size;
  if (top_idx >= file_buffer_len)
    top_idx = 0;

  fclose(f);
}

// Scroll up when deleting characters at the top of the buffer.
// Updates `top_idx`.
__attribute__((always_inline))
static inline void backspace_scroll_up()
{
  if (top_idx <= 1) return;
  char *p = rstrchr(file_buffer, file_buffer + top_idx - 2, '\n');
  if (*p == '\n') ++p;
  uint32_t pi = p - file_buffer;
  top_idx = pi;
}

// Scroll down to the next newline. Updates `top_idx`, returns
// new cursor index.
__attribute__((always_inline))
static inline uint32_t newline_scroll_down()
{
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;
  char *n = strchr(file_buffer + top_idx, '\n');
  if (n == NULL || n[1] == 0) return cursor_idx;
  uint32_t ni = n - file_buffer + 1;
  uint32_t num_lines_moved = ((ni - top_idx) / line_len) + 1;
  top_idx = ni;
  return ((cursor_idx / line_len) - num_lines_moved + 1) * line_len;
}

// Scroll up. Updates `lines`, `top_idx`, `buffer_idx`, `cursor_idx`.
static void scroll_up()
{
  if (top_idx == 0) return;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  // Scrolling {
  uint32_t previous_newline = top_idx - 1;
  char *previous_previous_newline = rstrchr(
    file_buffer, file_buffer + previous_newline - 1, '\n'
    );
  if (*previous_previous_newline == '\n') // we found a newline
    top_idx = previous_previous_newline - file_buffer + 1;
  else // we didn't find a newline, move to the beginning of the buffer
    top_idx = 0;

  update_lines(0, top_idx);
  // }

  // Moving {
  if (cursor_idx >= lines[0].len)
    cursor_idx = lines[0].len - 1;
  buffer_idx = top_idx + cursor_idx;
  // }
}

// Scroll down. Updates `lines`, `top_idx`, `buffer_idx`, `cursor_idx`.
static void scroll_down()
{
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  // Scrolling {
  char *next_newline = strchr(file_buffer + top_idx, '\n');
  if (next_newline == NULL) return;
  if (next_newline[1] == 0) return; // next newline is the last character
  uint32_t new_top_idx = next_newline - file_buffer + 1;
  uint32_t num_lines_moved = ((new_top_idx - top_idx) / line_len) + 1;
  top_idx = new_top_idx;
  update_lines(0, top_idx);
  // }

  // Moving {
  cursor_idx -= num_lines_moved * line_len;
  // }
}

// Update footer text based on command state.
static void update_footer_text()
{
  char *str = NULL;
  switch (cs) {
  case CS_NORMAL:
    str = "- :: [e]* | [v]+ | [p] | [s] | [o] | [q]";
    break;
  case CS_EDIT:
    str = "* :: [ESC]-";
    break;
  case CS_SELECT:
    str = "+ :: [ESC]- | [c] | [k]";
    break;
  case CS_CONFIRM_SAVE:
    str = "Save? ([ESC]cancel) [y/n]"; break;
  case CS_SAVE_PATH:
  case CS_OPEN_PATH:
    str = "Path: ([ESC]cancel)"; break;
  }
  strncpy(footer_text, str, FOOTER_LEN);
}

// Move the cursor up one line. Updates `cursor_idx` and `buffer_idx`.
static void move_up()
{
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  uint32_t cy = cursor_idx / line_len;
  uint32_t cx = cursor_idx % line_len;

  if (cy == 0) return;

  cursor_idx -= line_len;
  --cy;
  if (cx >= lines[cy].len) {
    cx = lines[cy].len - 1;
    cursor_idx = (cy * line_len) + cx;
  }
  buffer_idx = lines[cy].buffer_idx + cx;
}

// Move the cursor down one line. Updates `cursor_idx` and `buffer_idx`.
static void move_down()
{
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

  uint32_t cy = cursor_idx / line_len;
  uint32_t cx = cursor_idx % line_len;

  if (cy == num_lines - 1) return;
  if (lines[cy + 1].len < 0) return;

  cursor_idx += line_len;
  ++cy;
  if (cx >= lines[cy].len) {
    cx = lines[cy].len - 1;
    cursor_idx = (cy * line_len) + cx;
  }
  buffer_idx = lines[cy].buffer_idx + cx;
}

// Handle keyboard input
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
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;
  uint32_t cursor_x = cursor_idx % line_len;
  uint32_t cursor_y = cursor_idx / line_len;
  uint32_t old_cursor_idx = cursor_idx;
  uint8_t update = 1;
  uint8_t moved = 1;

#define MOVEMENT                                                        \
  switch (code) {                                                       \
  case KB_SC_LEFT:                                                      \
    if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }  \
    if (file_buffer[buffer_idx - 1] == '\n') { update = 0; break; }     \
    --cursor_idx;                                                       \
    --buffer_idx;                                                       \
    update_cursor(old_cursor_idx, 1);                                   \
    break;                                                              \
  case KB_SC_RIGHT:                                                     \
    if (file_buffer == NULL || buffer_idx == file_buffer_len) {         \
      update = 0; break;                                                \
    }                                                                   \
    if (file_buffer[buffer_idx] == '\n') { update = 0; break; }         \
    if (cursor_y == num_lines - 1 && cursor_x == line_len - 1) {        \
      update = 0; break;                                                \
    }                                                                   \
    ++cursor_idx;                                                       \
    ++buffer_idx;                                                       \
    update_cursor(old_cursor_idx, 1);                                   \
    break;                                                              \
  case KB_SC_UP:                                                        \
    if (file_buffer == NULL || buffer_idx == 0) { update = 0; break; }  \
    if (cursor_y == 0) {                                                \
      scroll_up();                                                      \
      render_buffer(0);                                                 \
      update_cursor(old_cursor_idx, 0);                                 \
      break;                                                            \
    }                                                                   \
    move_up();                                                          \
    if (cursor_idx != old_cursor_idx)                                   \
      update_cursor(old_cursor_idx, 1);                                 \
    break;                                                              \
  case KB_SC_DOWN:                                                      \
    if (file_buffer == NULL) { update = 0; break; }                     \
    if (cursor_y == num_lines - 1) {                                    \
      scroll_down();                                                    \
      render_buffer(0);                                                 \
      update_cursor(old_cursor_idx, 0);                                 \
      break;                                                            \
    }                                                                   \
    move_down();                                                        \
    if (cursor_idx != old_cursor_idx)                                   \
      update_cursor(old_cursor_idx, 1);                                 \
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
      if (file_buffer == NULL || file_buffer_len >= file_buffer_capacity) {
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
      update_lines(cursor_y, buffer_idx - cursor_x);
      render_buffer(cursor_y);
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
    uint32_t old_cursor_idx = cursor_idx;
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
          update_lines(0, top_idx);
          render_buffer(0);
          cursor_idx = buffer_idx - top_idx;
          update_cursor(old_cursor_idx, 0);
          break;
        }
        old_line_len = lines[cursor_y - 1].len;
        update_lines(cursor_y - 1, lines[cursor_y - 1].buffer_idx);
        render_buffer(cursor_y - 1);
        cursor_idx = ((cursor_y - 1) * line_len) + old_line_len - 1;
        update_cursor(old_cursor_idx, 0);
        break;
      }
      --cursor_idx;
      cursor_y = cursor_idx / line_len;
      update_lines(cursor_y, lines[cursor_y].buffer_idx);
      render_buffer(cursor_y);
      update_cursor(old_cursor_idx, 0);
      break;
    case KB_SC_TAB:
      if (file_buffer == NULL || file_buffer_len >= file_buffer_capacity) {
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
        cursor_idx = newline_scroll_down();
        cursor_idx += cursor_x == line_len - 1;
        update_lines(0, top_idx);
        render_buffer(0);
        update_cursor(old_cursor_idx, 0);
        break;
      }
      update_lines(cursor_y, lines[cursor_y].buffer_idx);
      render_buffer(cursor_y);
      cursor_idx += 2;
      update_cursor(old_cursor_idx, 0);
      break;
    default:
      if (file_buffer == NULL || file_buffer_len >= file_buffer_capacity) {
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
        cursor_idx = newline_scroll_down();
        update_lines(0, top_idx);
        render_buffer(0);
        update_cursor(old_cursor_idx, 0);
        break;
      }
      update_lines(cursor_y, lines[cursor_y].buffer_idx);
      render_buffer(cursor_y);
      if (inserted == '\n') cursor_idx = (cursor_y + 1) * line_len;
      else ++cursor_idx;
      update_cursor(old_cursor_idx, 0);
      break;
    }
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_SELECT) {
    return;
/*     uint32_t old_cursor_idx = cursor_idx; */
/*     uint32_t old_top_idx = top_idx; */
/*     MOVEMENT; */
/*     if (moved) { */
/*       if (update) { */
/*         int32_t delta = 0; */
/*         if (top_idx != old_top_idx) { */
/*           int32_t ti = top_idx; */
/*           int32_t oti = old_top_idx; */
/*           int32_t diff = ti - oti; */
/*           diff += diff > 0 ? -1 : 1; */
/*           delta = (int32_t)round((double)(diff) / line_len) */
/*             + (top_idx < old_top_idx ? -1 : 1); */
/*         } */
/*         selection_line_delta += delta; */
/*         clear_selection(delta, old_cursor_idx, cursor_idx); */
/*         render_selection(selection_line_delta); */
/*         ui_swap_buffers((uint32_t)ui_buf); */
/*       } */
/*       return; */
/*     } */
/*     uint32_t selection_len = buffer_idx - selection_buffer_idx; */
/*     uint32_t lower = selection_buffer_idx; */
/*     if (selection_buffer_idx > buffer_idx) { */
/*       selection_len = selection_buffer_idx - buffer_idx; */
/*       lower = buffer_idx; */
/*     } */
/*     uint32_t lci = cursor_idx; */
/*     if (selection_cursor_idx < cursor_idx) lci = selection_cursor_idx; */

/* #define CLEAR                                                   \ */
/*     cs = CS_NORMAL;                                             \ */
/*     clear_selection(                                            \ */
/*       selection_line_delta, selection_cursor_idx, cursor_idx    \ */
/*       );                                                        \ */
/*     update_footer_text();                                       \ */
/*     render_footer(); */

/* #define COPY                                                        \ */
/*     if (selection_len == 0) break;                                  \ */
/*     if (paste_buffer == NULL || paste_buffer_len < selection_len) { \ */
/*       paste_buffer_len = selection_len;                             \ */
/*       paste_buffer = realloc(paste_buffer, paste_buffer_len + 1);   \ */
/*     }                                                               \ */
/*     memcpy(paste_buffer, file_buffer + lower, selection_len);       \ */
/*     paste_buffer[selection_len] = '\0';                             \ */

/*     switch (code) { */
/*     case KB_SC_C: */
/*       COPY; */
/*       CLEAR; */
/*       break; */
/*     case KB_SC_K: */
/*       COPY; */
/*       memmove( */
/*         file_buffer + lower, */
/*         file_buffer + lower + selection_len, */
/*         file_buffer_len - lower - selection_len */
/*         ); */
/*       file_buffer_len -= selection_len; */
/*       if (buffer_idx > lower) buffer_idx -= selection_len; */
/*       file_buffer[file_buffer_len] = '\0'; */
/*       if (buffer_dirty == 0) { */
/*         buffer_dirty = 1; */
/*         render_path(); */
/*       } */
/*       if (lower < top_idx) { */
/*         top_idx = selection_top_idx; */
/*         buffer_idx = selection_buffer_idx; */
/*         update_line_lengths(0, top_idx); */
/*         render_buffer(0, top_idx); */
/*         update_cursor(selection_cursor_idx, 0); */
/*       } else { */
/*         buffer_idx = lower; */
/*         update_line_lengths(lci / line_len, lower - (lci % line_len)); */
/*         render_buffer(lci / line_len, lower - (lci % line_len)); */
/*         update_cursor(lci, 0); */
/*       } */
/*       CLEAR; */
/*       break; */
/*     case KB_SC_ESC: */
/*       CLEAR; */
/*       break; */
/*     default: update = 0; */
/*     } */
/*     if (update) ui_swap_buffers((uint32_t)ui_buf); */
/*     return; */
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
      update_lines(0, 0);
      render_buffer(0);
      cursor_idx = 0;
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
        if (field_len == 0) { update = 0; break; }
        uint8_t type = check_path(footer_field);
        if (errno == ENOENT) {
          FILE *f = fopen(footer_field, "rw+");
          if (f == NULL) { update = 0; break; }
          fclose(f);
          type = check_path(footer_field);
        }
        if (type == 0) { update = 0; break; }
        if (type == 1) {
          free(file_path);
          char buf[1024];
          resolve(buf, footer_field, 1024);
          file_path = strdup(buf);
        } else {
          free(dir_path);
          dir_path = strdup(footer_field);
          ui_split(UI_SPLIT_LEFT);
        }
        cs = CS_NORMAL;
        update_footer_text();
        render_footer();
        memset(footer_field, 0, sizeof(footer_field));
        load_file(); buffer_dirty = 0; buffer_idx = top_idx;
        render_path();
        update_lines(0, top_idx);
        render_buffer(0);
        cursor_idx = 0;
        update_cursor(0, 0);
      });
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    return;
  }

  if (cs == CS_SAVE_PATH) {
    FIELD_INPUT({
        if (field_len == 0) { update = 0; break; }
        free(file_path);
        char buf[1024];
        resolve(buf, footer_field, 1024);
        file_path = strdup(buf);
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

  memset(lines, 0, sizeof(lines));

  fill_color(ui_buf, BG_COLOR, window_w * window_h);
  if (cs == CS_SELECT) cs = CS_NORMAL;
  load_file();
  buffer_dirty = 0;
  update_lines(0, top_idx);
  buffer_idx = top_idx;
  render_path();
  render_footer();
  render_buffer(0);
  cursor_idx = 0;
  update_cursor(0, 0);
  buffer_idx = top_idx;
  if (ev.is_active == 0) render_inactive();

  if (dir_path) {
    if (fork() == 0) {
      char *args[2] = { dir_path, NULL };
      execve("/apps/dex", args, environ);
      exit(1);
    }
    free(dir_path);
    dir_path = NULL;
  }

  ui_swap_buffers((uint32_t)ui_buf);
}

int main(int argc, char *argv[])
{
  if (argc > 1) {
    char buf[1024];
    int32_t res = resolve(buf, argv[1], 1024);
    if (res == 0) file_path = strdup(buf);
  }

  memset(footer_text, 0, sizeof(footer_text));
  // memset(line_lengths, 0, sizeof(line_lengths));
  memset(footer_field, 0, sizeof(footer_field));
  load_file(); buffer_dirty = 0;
  cs = CS_NORMAL;
  update_footer_text();

  ui_init();
  ui_set_handler(ui_handler);
  int32_t res = ui_acquire_window();
  if (res) return 1;

  while (1) ui_wait();

  return 0;
}
