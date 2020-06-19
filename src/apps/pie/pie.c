
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

#define FOOTER_LEN    (SCREENWIDTH / FONTWIDTH)
#define LINE_HEIGHT   (FONTHEIGHT + FONTVPADDING)
#define MAX_NUM_LINES (SCREENHEIGHT / (FONTHEIGHT + FONTVPADDING))

static const uint32_t BG_COLOR       = 0xffffff;
static const uint32_t TEXT_COLOR     = 0;
static const uint32_t INACTIVE_COLOR = 0xb0b0b0;
static const uint32_t PATH_HEIGHT    = 24;
static const uint32_t FOOTER_HEIGHT  = 24;
static const uint32_t TOTAL_PADDING  = 16;

// window state
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;
static volatile uint32_t ui_lock = 0;

// Current executable path
static char *path = NULL;

// 'Command' (footer) state
typedef enum {
  CS_PENDING,
  CS_EXEC
} command_state_t;
static command_state_t cs;
static char footer_text[FOOTER_LEN];
static char footer_field[FOOTER_LEN];

// lines and text buffer
typedef struct line_s {
  uint32_t buffer_idx; // index in text buffer
  int32_t len;         // length of the line
} line_t;
static line_t lines[MAX_NUM_LINES];
static char *text_buffer = NULL;
static uint32_t buffer_len = 0;
static uint32_t top_idx = 0; // buffer index of first character on screen
static uint32_t screen_line_idx = 0; // (y) index of last line

// child process state
static uint32_t proc_write_fd = 0;
static uint32_t proc_read_fd = 0;
static pid_t proc_pid = 0;

// set `n` pixels of the buffer to `b` starting at `p`
__attribute__((always_inline))
static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{ for (uint32_t i = 0; i < n; ++i) p[i] = b; }

// render text at x and y coordinates
static void render_text(const char *text, uint32_t x, uint32_t y)
{
  size_t len = strlen(text);
  size_t w, h;
  text_dimensions(text, len, &w, &h);
  if (w * h == 0) return;

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
  char *str = path == NULL ? "[None]" : path;
  fill_color(ui_buf, BG_COLOR, window_w * PATH_HEIGHT);
  render_text(str, 4, 4);
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

// Render all the text in the buffer starting at line `line_idx`
static void render_buffer(uint32_t line_idx)
{
  if (text_buffer == NULL) return;
  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

  // filling the buffer area with the background color {
  uint32_t *top_row =
    ui_buf
    + (window_w * (PATH_HEIGHT + (TOTAL_PADDING / 2) + (line_idx * LINE_HEIGHT)));
  uint32_t fill_size =
    window_w * (
      window_h - PATH_HEIGHT - FOOTER_HEIGHT -
      (TOTAL_PADDING / 2) - (line_idx * LINE_HEIGHT)
      );
  if (line_idx == 0) {
    top_row = ui_buf + (window_w * (PATH_HEIGHT + 1));
    fill_size = window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1);
  }
  fill_color(top_row, BG_COLOR, fill_size);
  // }

  // rendering the lines {
  uint32_t top = PATH_HEIGHT + (TOTAL_PADDING / 2);
  for (uint32_t i = line_idx; i < num_lines && lines[i].len >= 0; ++i) {
    if (lines[i].len == 0) continue;
    uint32_t y = top + i * LINE_HEIGHT;
    char *line = strndup(text_buffer + lines[i].buffer_idx, lines[i].len);
    render_text(line, TOTAL_PADDING / 2, y);
    free(line);
  }
  // }
}

// Update the array of lines, starting at `line_idx` and index
// `buf_idx` of the buffer. Updates `screen_line_idx`.
static void update_lines(uint32_t line_idx, uint32_t buf_idx)
{
  if (text_buffer == NULL) return;

  uint32_t num_lines =
    (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  char *p = text_buffer + buf_idx;
  uint32_t i = line_idx;
  for (; i < num_lines && p < text_buffer + buffer_len; ++i) {
    lines[i].buffer_idx = p - text_buffer;
    char *next_nl = strchr(p, '\n');

    if (next_nl == NULL || next_nl - p >= line_len) {
      // wrap text around without newline
      lines[i].len = line_len;
      p += line_len;
      continue;
    }

    lines[i].len = next_nl - p + 1;
    p = next_nl + 1;
    screen_line_idx = i;
  }
  if (i < num_lines) lines[i].len = -1;
  lines[num_lines].len = -1;
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

// this thread reads from the child process and writes to the text buffer
static void exec_thread()
{
  close(proc_write_fd);
  while (1) {
    char buf[1024];
    int32_t r = read(proc_read_fd, buf, 1024);
    if (r <= 0) break;

    thread_lock(&ui_lock);

    uint32_t num_lines =
      (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

    uint8_t scrolled = 0;

    if (screen_line_idx >= num_lines - 1) { // have to scroll
      top_idx = buffer_len - lines[screen_line_idx].len;
      scrolled = 1;
    }

    if (buffer_len + r >= 0x2000) { // have to realloc buffer
      char *new = (char *)pagealloc(2);
      memcpy(new, text_buffer + top_idx, buffer_len - top_idx);
      memset(new, 0, 0x2000 - (buffer_len - top_idx));
      pagefree((uint32_t)text_buffer, 2);
      text_buffer = new;
      buffer_len -= top_idx;
      top_idx = 0;
      scrolled = 1;
    }

    memcpy(text_buffer + buffer_len, buf, r);
    buffer_len += r;
    text_buffer[buffer_len] = 0;

    if (scrolled) {
      update_lines(0, top_idx);
      render_buffer(0);
    } else {
      uint32_t old_screen_line_idx = screen_line_idx;
      update_lines(screen_line_idx, lines[screen_line_idx].buffer_idx);
      render_buffer(old_screen_line_idx);
    }

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

  close(proc_read_fd);
  proc_read_fd = 0;
  proc_write_fd = 0;
  proc_pid = 0;
  thread_unlock(&ui_lock);
}

// Execute the specified file and start a thread to read its output.
static uint8_t exec_path(char **args)
{
  uint32_t readfd, writefd;
  int32_t res = pipe(&readfd, &writefd, 0, 1);
  if (res) return 0;

  uint32_t readfd2, writefd2;
  res = pipe(&readfd2, &writefd2, 1, 1);
  if (res) return 0;

  proc_pid = fork();
  if (proc_pid == 0) {
    close(readfd);
    close(writefd2);

    uint32_t errfd = dup(writefd);
    movefd(readfd2, 0);
    movefd(writefd, 1);
    movefd(errfd, 2);

    maketty(0);
    maketty(1);
    maketty(2);

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

// check that the path is an executable file
static uint8_t check_path(char *p)
{
  struct stat st;
  int32_t res = stat(p, &st);
  if (res) return 0;
  if ((st.st_dev & 1) == 0) return 0;
  return 1;
}

// handle keyboard interrupts
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
    // we are not executing a program -- have to execute the specified program
    // with provided arguments

    FIELD_INPUT({
        if (
          strcmp(footer_field, "q") == 0
          && (window_w != SCREENWIDTH || window_h != SCREENHEIGHT)
          ) exit(0);

        char **args = malloc(sizeof(char *) * field_len);
        char *tmp = malloc(field_len + 1);
        memcpy(tmp, footer_field, field_len + 1);

        // split by space
        uint32_t i = 0;
        uint32_t args_len = 0;
        for (; i < field_len; ++i) {
          if (tmp[i] == ' ') {
            tmp[i] = 0;
            args[args_len] = tmp + i + 1;
            ++args_len;
          }
        }
        args[args_len] = 0;
        tmp[i] = 0;

        uint8_t valid = check_path(tmp);
        if (!valid) { update = 0; free(tmp); free(args); break; }

        char buf[1024];
        int32_t res = resolve(buf, tmp, 1024);
        if (res) { update = 0; free(tmp); free(args); break; }
        free(path);
        path = strdup(buf);
        render_path();

        valid = exec_path(args);
        free(tmp); free(args);
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
    // we are currently executing a program -- have to write input to screen
    // and forward it to the program

    uint8_t killed = 0;
    switch (code) {
    case KB_SC_ESC:
      if (proc_pid) {
        killed = 1;
        signal_send(proc_pid, SIGKILL);
      }
      break;
    }
    if (killed) {
      thread_unlock(&ui_lock);
      return;
    }

    FIELD_INPUT({
        char *buf = malloc(field_len + 2);
        memcpy(buf, footer_field, field_len);
        buf[field_len] = '\n';
        write(proc_write_fd, buf, field_len + 1);

        uint32_t num_lines =
          (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / LINE_HEIGHT;

        uint8_t scrolled = 0;

        if (screen_line_idx >= num_lines - 1) { // have to scroll
          top_idx = buffer_len - lines[screen_line_idx].len;
          scrolled = 1;
        }

        if (buffer_len + field_len + 1 >= 0x2000) { // have to realloc buffer
          char *new = (char *)pagealloc(2);
          memcpy(new, text_buffer + top_idx, buffer_len - top_idx);
          memset(new, 0, 0x2000 - (buffer_len - top_idx));
          pagefree((uint32_t)text_buffer, 2);
          text_buffer = new;
          buffer_len -= top_idx;
          top_idx = 0;
          scrolled = 1;
        }

        memcpy(text_buffer + buffer_len, buf, field_len + 1);
        buffer_len += field_len + 1;
        text_buffer[buffer_len] = 0;

        if (scrolled) {
          update_lines(0, top_idx);
          render_buffer(0);
        } else {
          uint32_t old_screen_line_idx = screen_line_idx;
          update_lines(screen_line_idx, lines[screen_line_idx].buffer_idx);
          render_buffer(old_screen_line_idx);
        }

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

// handle UI event
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
  int32_t res = ui_acquire_window();
  if (res) return 1;

  while (1) ui_wait();

  return 0;
}
