
// pie.c
//
// Program Interaction Environment.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>
#include <mako.h>
#include <ui.h>
#include "text_render.h"
#include "scancode.h"

#define FOOTER_LEN          99
#define LINE_LEN_TABLE_SIZE 40

static const uint32_t BG_COLOR       = 0xffffff;
static const uint32_t TEXT_COLOR     = 0;
static const uint32_t INACTIVE_COLOR = 0xb0b0b0;
static const uint32_t PATH_HEIGHT    = 24;
static const uint32_t FOOTER_HEIGHT  = 24;
static const uint32_t LINE_HEIGHT    = FONTHEIGHT + FONTVPADDING;

static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;
static volatile uint32_t ui_lock = 0;

static char *path = NULL;

typedef enum {
  CS_PENDING,
  CS_EXEC
} command_state_t;

static command_state_t cs;
static char footer_text[FOOTER_LEN];
static char footer_field[FOOTER_LEN];
static char *text_buffer = NULL;
static uint32_t line_lengths[LINE_LEN_TABLE_SIZE];
static uint32_t top_idx = 0;
static uint32_t buffer_len = 0;
static uint32_t screen_line_idx = 0;
static uint32_t proc_write_fd = 0;
static uint32_t proc_read_fd = 0;

__attribute__((always_inline))
static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{ for (uint32_t i = 0; i < n; ++i) p[i] = b; }

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

__attribute__((always_inline))
static inline void render_inactive()
{
  for (uint32_t i = 0; i < window_w; ++i)
    for (uint32_t j = 0; j < window_h; ++j)
      if (ui_buf[(j * window_w) + i] != BG_COLOR)
        ui_buf[(j * window_w) + i] = INACTIVE_COLOR;
}

static void render_path()
{
  char *str = path == NULL ? "[None]" : path;
  fill_color(ui_buf, BG_COLOR, window_w * PATH_HEIGHT);
  render_text(str, 4, 4);
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

  if (char_idx >= buffer_len) return;
  char *p = text_buffer + char_idx;

  uint32_t len = (window_w - 16) / FONTWIDTH;
  uint32_t ht = window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16;
  uint32_t top = PATH_HEIGHT + 8;

  char *buf = calloc(1, len + 1);
  uint32_t nh = 0;
  uint32_t llidx = line_idx;
  uint32_t y = LINE_HEIGHT * line_idx;
  for (
    ;
    y < ht && llidx < LINE_LEN_TABLE_SIZE && p < text_buffer + buffer_len;
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
  if (top_idx >= buffer_len) return;

  uint32_t len = (window_w - 16) / FONTWIDTH;
  for (
    uint32_t i = top_line;
    i < LINE_LEN_TABLE_SIZE && fidx < buffer_len;
    ++i
    )
  {
    char *p = text_buffer + fidx;
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
    } else if (nextnl == NULL && buffer_len - fidx < len)
      llen = buffer_len - fidx;
    fidx += llen + nl;
    line_lengths[i] = llen;
    screen_line_idx = i;
  }
}

static void update_footer_text()
{
  char *str = NULL;
  switch (cs) {
  case CS_PENDING: str = "(\"q\" to quit) % "; break;
  case CS_EXEC:    str = "$ "; break;
  }
  strncpy(footer_text, str, FOOTER_LEN);
}

static void exec_thread()
{
  close(proc_write_fd);
  while (1) {
    char buf[1024];
    int32_t r = read(proc_read_fd, buf, 1024);
    if (r <= 0) break;

    thread_lock(&ui_lock);
    //uint32_t line_idx = screen_line_idx;
    //uint32_t char_idx = buffer_len - line_lengths[line_idx];
    //if (text_buffer[char_idx] == '\n') ++char_idx;
    uint32_t num_lines =
      (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;
    if (screen_line_idx >= num_lines - 1)
      top_idx = buffer_len - line_lengths[screen_line_idx];
    if (buffer_len + r >= 0x2000) {
      char *new = (char *)pagealloc(2);
      memcpy(new, text_buffer + top_idx, buffer_len - top_idx);
      memset(new, 0, 0x2000 - (buffer_len - top_idx));
      pagefree((uint32_t)text_buffer, 2);
      text_buffer = new;
      buffer_len -= top_idx;
      top_idx = 0;
    }
    memcpy(text_buffer + buffer_len, buf, r);
    buffer_len += r;
    text_buffer[buffer_len] = 0;
    //update_line_lengths(line_idx, char_idx);
    //render_buffer(line_idx, char_idx);
    update_line_lengths(0, top_idx);
    render_buffer(0, top_idx);
    ui_swap_buffers((uint32_t)ui_buf);
    thread_unlock(&ui_lock);
  }

  thread_lock(&ui_lock);
  free(path);
  path = NULL;
  cs = CS_PENDING;
  update_footer_text();
  render_footer();
  render_path();
  ui_swap_buffers((uint32_t)ui_buf);
  thread_unlock(&ui_lock);

  close(proc_read_fd);
}

static uint8_t exec_path()
{
  uint32_t readfd, writefd;
  int32_t res = pipe(&readfd, &writefd, 0, 1);
  if (res) return 0;

  uint32_t readfd2, writefd2;
  res = pipe(&readfd2, &writefd2, 1, 1);
  if (res) return 0;

  pid_t p = fork();
  if (p == 0) {
    close(readfd);
    close(writefd2);

    uint32_t errfd = dup(writefd);
    movefd(readfd2, 0);
    movefd(writefd, 1);
    movefd(errfd, 2);

    maketty(0);
    maketty(1);
    maketty(2);

    char *args[] = { path, NULL };
    execve(path, args, environ);
    exit(1);
  }

  proc_read_fd = readfd;
  proc_write_fd = writefd2;
  close(writefd);
  close(readfd2);
  pid_t t = thread(exec_thread, NULL);
  close(readfd);

  return 1;
}

static uint8_t check_path(char *p)
{
  struct stat st;
  int32_t res = stat(p, &st);
  if (res) return 0;
  if ((st.st_dev & 1) == 0) return 0;
  return 1;
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
      thread_lock(&ui_lock);
      meta = 0; lshift = 0; rshift = 0; capslock = 0;
      render_inactive();
      ui_swap_buffers((uint32_t)ui_buf);
      ui_yield();
      thread_unlock(&ui_lock);
      return;
    }
  }

  thread_lock(&ui_lock);
  uint8_t update = 1;
  size_t field_len = strlen(footer_field);
  char field_char = 0;

#define FIELD_INPUT(action) {                                           \
    switch (code) {                                                     \
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

  if (cs == CS_PENDING) {
    FIELD_INPUT({
        if (strcmp(footer_field, "q") == 0) exit(0);

        uint8_t valid = check_path(footer_field);
        if (!valid) { update = 0; break; }

        char buf[1024];
        int32_t res = resolve(buf, footer_field, 1024);
        if (res) { update = 0; break; }
        free(path);
        path = strdup(buf);
        render_path();

        valid = exec_path();
        if (!valid) { update = 0; break; }
        memset(footer_field, 0, sizeof(footer_field));
        cs = CS_EXEC;
        update_footer_text();
        render_footer();
      });
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_EXEC) {
    FIELD_INPUT({
        char *buf = malloc(field_len + 2);
        memcpy(buf, footer_field, field_len);
        buf[field_len] = '\n';
        write(proc_write_fd, buf, field_len + 1);

        // uint32_t line_idx = screen_line_idx;
        // uint32_t char_idx = buffer_len - line_lengths[line_idx];
        // if (text_buffer[char_idx] == '\n') ++char_idx;
        uint32_t num_lines =
          (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 16) / LINE_HEIGHT;
        if (screen_line_idx >= num_lines - 1)
          top_idx = buffer_len - line_lengths[screen_line_idx];
        if (buffer_len + field_len + 1 >= 0x2000) {
          char *new = (char *)pagealloc(2);
          memcpy(new, text_buffer + top_idx, buffer_len - top_idx);
          memset(new, 0, 0x2000 - (buffer_len - top_idx));
          pagefree((uint32_t)text_buffer, 2);
          text_buffer = new;
          buffer_len -= top_idx;
          top_idx = 0;
        }
        memcpy(text_buffer + buffer_len, buf, field_len + 1);
        buffer_len += field_len + 1;
        text_buffer[buffer_len] = 0;
        // update_line_lengths(line_idx, char_idx);
        // render_buffer(line_idx, char_idx);
        update_line_lengths(0, top_idx);
        render_buffer(0, top_idx);

        free(buf);
        memset(footer_field, 0, sizeof(footer_field));
        update_footer_text();
        render_footer();
      });
    if (update) ui_swap_buffers((uint32_t)ui_buf);
    thread_unlock(&ui_lock);
    return;
  }
}

static void ui_handler(ui_event_t ev)
{
  if (ev.type == UI_EVENT_KEYBOARD) {
    keyboard_handler(ev.code);
    return;
  }

  thread_lock(&ui_lock);

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
  render_path();
  render_footer();
  if (ev.is_active == 0) render_inactive();

  ui_swap_buffers((uint32_t)ui_buf);
  thread_unlock(&ui_lock);
}

void sigpipe_handler()
{
  fprintf(stderr, "sigpipe\n");
}

int main(int argc, char *argv[])
{
  if (argc > 1) {
    char buf[1024];
    int32_t res = resolve(buf, argv[1], 1024);
    if (res == 0) path = strdup(buf);
  }

  signal(SIGPIPE, sigpipe_handler);

  memset(footer_text, 0, sizeof(footer_text));
  memset(footer_field, 0, sizeof(footer_field));

  text_buffer = (char *)pagealloc(2);
  memset(text_buffer, 0, 0x2000);

  update_footer_text();

  ui_init();
  ui_set_handler(ui_handler);
  ui_acquire_window();

  while (1) ui_wait();

  return 0;
}
