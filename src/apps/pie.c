
// pie.c
//
// Program Interaction Environment.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/scancode.h"
#include <errno.h>
#include <mako.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ui.h>
#include <unistd.h>

// FIXME remove thes
#define FONTWIDTH 8
#define FONTHEIGHT 14

#define FOOTER_LEN (SCREENWIDTH / FONTWIDTH)
#define MAX_NUM_LINES (SCREENHEIGHT / FONTHEIGHT)

static const uint32_t BG_COLOR = 0xffffff;
static const uint32_t DIVIDER_COLOR = 0xb0b0b0;
static const uint32_t PATH_HEIGHT = 24;
static const uint32_t FOOTER_HEIGHT = 24;
static const uint32_t TOTAL_PADDING = 16;

// window state
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static uint32_t *ui_buf = NULL;
static volatile uint32_t ui_lock = 0;

// Current executable path
static char *path = NULL;

// Path and arguments with which to launch app
static char *app_path = NULL;
static char **app_args = NULL;

// 'Command' (footer) state
typedef enum
{
  CS_PENDING,
  CS_EXEC
} command_state_t;
static command_state_t cs;
static char footer_text[FOOTER_LEN];
static char footer_field[FOOTER_LEN];

// lines and text buffer
typedef struct line_s
{
  uint32_t buffer_idx; // index in text buffer
  int32_t len;         // length of the line
} line_t;
static line_t lines[MAX_NUM_LINES];
static char *text_buffer = NULL;
static uint32_t buffer_len = 0;
static uint32_t top_idx = 0;         // buffer index of first character on screen
static uint32_t screen_line_idx = 0; // (y) index of last line

// child process state
static uint32_t proc_write_fd = 0;
static uint32_t proc_read_fd = 0;
static pid_t proc_pid = 0;

// set `n` pixels of the buffer to `b` starting at `p`
__attribute__((always_inline)) static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{
  for (uint32_t i = 0; i < n; ++i)
    p[i] = b;
}

// render text at x and y coordinates
static void render_text(const char *text, uint32_t x, uint32_t y)
{
  size_t len = strlen(text);
  uint32_t *p = ui_buf + (y * window_w) + x;
  ui_render_text(p, window_w, text, len, UI_FONT_CONSOLAS, 0);
}

// render the path bar at the top of the window
static void render_path()
{
  char *str = path == NULL ? "[None]" : path;
  fill_color(ui_buf, BG_COLOR, window_w * PATH_HEIGHT);
  render_text(str, 4, 4);
  uint32_t *line_row = ui_buf + (PATH_HEIGHT * window_w);
  fill_color(line_row, DIVIDER_COLOR, window_w);
}

// render the footer at the bottom of the window
static void render_footer()
{
  uint32_t *line_row = ui_buf + ((window_h - FOOTER_HEIGHT) * window_w);
  fill_color(line_row, BG_COLOR, window_w * FOOTER_HEIGHT);
  render_text(footer_text, 4, window_h - FOOTER_HEIGHT + 4);
  fill_color(line_row, DIVIDER_COLOR, window_w);
}

// Render all the text in the buffer starting at line `line_idx`
static void render_buffer(uint32_t line_idx)
{
  if (text_buffer == NULL)
    return;
  uint32_t num_lines = (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / FONTHEIGHT;

  // filling the buffer area with the background color {
  uint32_t *top_row =
    ui_buf + (window_w * (PATH_HEIGHT + (TOTAL_PADDING / 2) + (line_idx * FONTHEIGHT)));
  uint32_t fill_size = window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - (TOTAL_PADDING / 2) -
                                   (line_idx * FONTHEIGHT));
  if (line_idx == 0) {
    top_row = ui_buf + (window_w * (PATH_HEIGHT + 1));
    fill_size = window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1);
  }
  fill_color(top_row, BG_COLOR, fill_size);
  // }

  // rendering the lines {
  uint32_t top = PATH_HEIGHT + (TOTAL_PADDING / 2);
  for (uint32_t i = line_idx; i < num_lines && lines[i].len >= 0; ++i) {
    if (lines[i].len == 0)
      continue;
    uint32_t y = top + i * FONTHEIGHT;
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
  if (text_buffer == NULL)
    return;

  uint32_t num_lines = (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / FONTHEIGHT;
  uint32_t line_len = (window_w - TOTAL_PADDING) / FONTWIDTH;

  char *p = text_buffer + buf_idx;
  screen_line_idx = line_idx;
  uint32_t i = line_idx;
  lines[i].buffer_idx = buf_idx;
  for (; i < num_lines && p < text_buffer + buffer_len; ++i) {
    lines[i].buffer_idx = p - text_buffer;
    char *next_nl = strchr(p, '\n');

    if (next_nl == NULL || next_nl - p >= (int32_t)line_len) {
      // wrap text around without newline
      lines[i].len = line_len;
      p += line_len;
      continue;
    }

    lines[i].len = next_nl - p + 1;
    p = next_nl + 1;
    screen_line_idx = i;
  }
  if (i < num_lines)
    lines[i].len = -1;
  lines[num_lines].len = -1;
}

static void update_footer_text()
{
  char *str = NULL;
  char *wd = getcwd(NULL, 0);
  size_t wlen = strlen(wd);
  switch (cs) {
    case CS_PENDING:
      str = " % ";
      break;
    case CS_EXEC:
      str = " $ ";
      break;
  }
  strncpy(footer_text, wd, FOOTER_LEN);
  strncpy(footer_text + wlen, str, FOOTER_LEN - wlen);
  free(wd);
}

// this thread reads from the child process and writes to the text buffer
static void exec_thread()
{
  close(proc_write_fd);
  while (1) {
    char buf[1024];
    int32_t r = read(proc_read_fd, buf, 1024);
    if (r <= 0)
      break;
    thread_lock(&ui_lock);

    uint32_t num_lines = (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / FONTHEIGHT;

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

    ui_redraw_rect(0, 0, window_w, window_h);

    thread_unlock(&ui_lock);
  }

  thread_lock(&ui_lock);
  free(path);
  path = NULL;
  cs = CS_PENDING;
  update_footer_text();
  render_footer();
  render_path();
  ui_redraw_rect(0, 0, window_w, window_h);

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
  int32_t res = pipe(&readfd, &writefd);
  if (res)
    return 0;

  uint32_t readfd2, writefd2;
  res = pipe(&readfd2, &writefd2);
  if (res)
    return 0;

  proc_pid = fork();
  if (proc_pid == 0) {
    close(readfd);
    close(writefd2);

    uint32_t errfd = dup(writefd);
    movefd(readfd2, 0);
    movefd(writefd, 1);
    movefd(errfd, 2);

    execve(path, args, environ);
    exit(1);
  }

  proc_read_fd = readfd;
  proc_write_fd = writefd2;
  close(writefd);
  close(readfd2);
  thread(exec_thread, NULL);
  close(readfd);

  return 1;
}

// check that the path is an executable file
static uint8_t check_path(char *p)
{
  struct stat st;
  int32_t res = stat(p, &st);
  if (res)
    return 0;
  if ((st.st_dev & 1) == 0)
    return 0;
  return 1;
}

// search for a program in the specified path list ($PATH or $APPS_PATH)
static char *find_path(char *name, char *path_)
{
  char *path = strdup(path_);
  size_t name_len = strlen(name);
  size_t path_len = strlen(path);

  for (uint32_t i = 0; i < path_len; ++i)
    if (path[i] == ':')
      path[i] = 0;

  size_t step = strlen(path);
  for (uint32_t i = 0; i < path_len; i += step + 1) {
    step = strlen(path + i);
    if (strncmp(path + i, name, step) == 0 && check_path(name))
      return name;

    char *this_path = malloc(step + 1 + name_len + 1);
    memcpy(this_path, path + i, step);
    this_path[step] = '/';
    memcpy(this_path + step + 1, name, name_len);
    this_path[step + 1 + name_len] = 0;

    if (check_path(this_path)) {
      free(path);
      return this_path;
    }

    free(this_path);
  }

  free(path);
  return NULL;
}

// handle keyboard interrupts
static void keyboard_handler(uint8_t code)
{
  static uint8_t lshift = 0;
  static uint8_t rshift = 0;
  static uint8_t capslock = 0;

  if (code & 0x80) {
    code &= 0x7F;
    switch (code) {
      case KB_SC_LSHIFT:
        lshift = 0;
        break;
      case KB_SC_RSHIFT:
        rshift = 0;
        break;
    }
    return;
  }

  switch (code) {
    case KB_SC_LSHIFT:
      lshift = 1;
      return;
    case KB_SC_RSHIFT:
      rshift = 1;
      return;
    case KB_SC_CAPSLOCK:
      capslock = !capslock;
      return;
  }

  thread_lock(&ui_lock);
  uint8_t update = 1;
  size_t field_len = strlen(footer_field);
  char field_char = 0;

#define FIELD_INPUT(action)                                                                        \
  {                                                                                                \
    switch (code) {                                                                                \
      case KB_SC_BS:                                                                               \
        if (field_len == 0) {                                                                      \
          update = 0;                                                                              \
          break;                                                                                   \
        }                                                                                          \
        footer_field[field_len - 1] = 0;                                                           \
        update_footer_text();                                                                      \
        strcat(footer_text, footer_field);                                                         \
        render_footer();                                                                           \
        break;                                                                                     \
      case KB_SC_ENTER:                                                                            \
        action;                                                                                    \
        break;                                                                                     \
      default:                                                                                     \
        field_char = scancode_to_ascii(code, lshift || rshift || capslock);                        \
        if (field_char == 0 || field_len == FOOTER_LEN) {                                          \
          update = 0;                                                                              \
          break;                                                                                   \
        }                                                                                          \
        footer_field[field_len] = field_char;                                                      \
        footer_field[field_len + 1] = 0;                                                           \
        update_footer_text();                                                                      \
        strcat(footer_text, footer_field);                                                         \
        render_footer();                                                                           \
        break;                                                                                     \
    }                                                                                              \
  }

  if (cs == CS_PENDING) {
    // we are not executing a program -- have to execute the specified program
    // with provided arguments

    FIELD_INPUT({
      if (strcmp(footer_field, "q") == 0)
        exit(0);

      if (field_len == 0) {
        update = 0;
        break;
      }

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

      if (strcmp(tmp, "cd") == 0) { // "cd" == change directories
        char *dir = args[0];
        if (dir == NULL || dir[0] == 0)
          dir = "/home";
        chdir(dir);
        free(tmp);
        free(args);
        memset(footer_field, 0, sizeof(footer_field));
        update_footer_text();
        render_footer();
        break;
      } else if (strcmp(tmp, "clear") == 0) { // "clear" == clear the screen
        free(tmp);
        free(args);
        memset(footer_field, 0, sizeof(footer_field));
        update_footer_text();
        render_footer();

        char *new = (char *)pagealloc(2);
        memset(new, 0, 0x2000);
        pagefree((uint32_t)text_buffer, 2);
        text_buffer = new;
        buffer_len = 0;
        top_idx = 0;
        update_lines(0, top_idx);
        render_buffer(0);
        break;
      }

      char *app_path = find_path(tmp, getenv("APPS_PATH"));
      if (app_path) {
        if (fork() == 0) {
          execve(app_path, args, environ);
          exit(1);
        }
        free(tmp);
        free(app_path);
        free(args);
        memset(footer_field, 0, sizeof(footer_field));
        update_footer_text();
        render_footer();
        break;
      }

      char *env_path = NULL;
      if (check_path(tmp))
        env_path = strdup(tmp);
      else
        env_path = find_path(tmp, getenv("PATH"));
      if (env_path == NULL) {
        update = 0;
        free(tmp);
        free(args);
        break;
      }

      char buf[1024];
      int32_t res = resolve(buf, env_path, 1024);
      free(env_path);
      if (res) {
        update = 0;
        free(tmp);
        free(args);
        break;
      }
      free(path);
      path = strdup(buf);
      render_path();

      uint8_t valid = exec_path(args);
      free(tmp);
      free(args);
      if (!valid) {
        update = 0;
        break;
      }
      memset(footer_field, 0, sizeof(footer_field));
      cs = CS_EXEC;
      update_footer_text();
      render_footer();
    });
    if (update)
      ui_redraw_rect(0, window_h - FOOTER_HEIGHT, window_w, FOOTER_HEIGHT);
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_EXEC) {
    // we are currently executing a program -- have to write input to screen
    // and forward it to the program

    if (code == KB_SC_ESC && proc_pid) {
      signal_send(proc_pid, SIGKILL);
      thread_unlock(&ui_lock);
      return;
    }

    FIELD_INPUT({
      char *buf = malloc(field_len + 2);
      memcpy(buf, footer_field, field_len);
      buf[field_len] = '\n';
      write(proc_write_fd, buf, field_len + 1);

      uint32_t num_lines = (window_h - PATH_HEIGHT - FOOTER_HEIGHT - TOTAL_PADDING) / FONTHEIGHT;

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
    if (update)
      ui_redraw_rect(0, 0, window_w, window_h);
    thread_unlock(&ui_lock);
    return;
  }
}

static void resize_handler(ui_event_t ev)
{
  thread_lock(&ui_lock);

  if (ev.width == window_w && ev.height == window_h) {
    thread_unlock(&ui_lock);
    return;
  }

  window_w = ev.width;
  window_h = ev.height;

  fill_color(ui_buf, BG_COLOR, window_w * window_h);

  char *new = (char *)pagealloc(2);
  memset(new, 0, 0x2000);
  pagefree((uint32_t)text_buffer, 2);
  text_buffer = new;
  buffer_len = 0;
  top_idx = 0;
  update_lines(0, top_idx);
  render_buffer(0);
  render_path();
  render_footer();

  ui_redraw_rect(0, 0, window_w, window_h);
  thread_unlock(&ui_lock);
}

void sigpipe_handler()
{
  fprintf(stderr, "sigpipe\n");
}

int main(int argc, char *argv[])
{
  priority(1);

  if (argc > 1) {
    char buf[1024];
    int32_t res = resolve(buf, argv[1], 1024);
    if (res == 0)
      path = strdup(buf);
  }

  signal(SIGPIPE, sigpipe_handler);

  memset(footer_text, 0, sizeof(footer_text));
  memset(footer_field, 0, sizeof(footer_field));

  text_buffer = (char *)pagealloc(2);
  memset(text_buffer, 0, 0x2000);

  update_footer_text();

  ui_buf = malloc((SCREENWIDTH >> 1) * (SCREENHEIGHT >> 1) * sizeof(uint32_t));
  int32_t res = ui_acquire_window(ui_buf, "pie", SCREENWIDTH >> 1, SCREENHEIGHT >> 1);
  if (res < 0)
    return 1;

  ui_event_t ev;
  res = ui_next_event(&ev);
  if (res < 0 || ev.type != UI_EVENT_WAKE)
    return 1;

  // Use the resize handler to render everything upon window creation.
  resize_handler(ev);

  while (1) {
    res = ui_next_event(&ev);
    if (res < 0)
      return 1;
    switch (ev.type) {
      case UI_EVENT_KEYBOARD:
        keyboard_handler(ev.code);
        break;
      case UI_EVENT_RESIZE_REQUEST:
        resize_handler(ev);
        break;
      default:;
    }
  }

  return 0;
}
