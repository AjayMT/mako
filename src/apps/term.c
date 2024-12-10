
// term.c
//
// Terminal emulator and shell.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "../common/scancode.h"
#include <mako.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ui.h>
#include <unistd.h>

static uint32_t *ui_buf = NULL;
static struct ui_scrollview view;

static const uint32_t background_color = 0xffffff;
static const uint32_t text_color = 0;
static const uint32_t cursor_w = 2;
static const uint32_t cursor_h = 13;
static const uint32_t line_height = 13;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

#define SMALL_BUFFER_SIZE 256

static char line_buf[SMALL_BUFFER_SIZE];
static unsigned line_idx = 0;

static bool executing_program = false;
static uint32_t prog_write_fd = 0;
static uint32_t prog_read_fd = 0;
static pid_t prog_pid = 0;
static volatile uint32_t ui_lock = 0;

void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
  for (uint32_t i = 0; i < h; ++i)
    memset32(view.content_buf + (y + i) * view.content_w + x, color, w);
}

void flip_cursor()
{
  for (uint32_t y = 0; y < cursor_h; ++y) {
    for (uint32_t x = 0; x < cursor_w; ++x) {
      uint32_t *pixel = view.content_buf + (cursor_y + y) * view.content_w + cursor_x + x;
      *pixel = background_color - *pixel;
    }
  }
}

void print(const char *text)
{
  size_t text_len = strlen(text);
  if (text_len == 0)
    return;
  flip_cursor();

  uint32_t w, h;
  ui_measure_text(&w, &h, text, text_len, UI_FONT_MONACO);

  ui_scrollview_grow(&view, cursor_x + w + cursor_w, cursor_y + h, 20);
  ui_render_text(view.content_buf + (cursor_y * view.content_w) + cursor_x,
                 view.content_w,
                 text,
                 text_len,
                 UI_FONT_MONACO);

  cursor_x += w;
  flip_cursor();
  ui_scrollview_redraw_rect(&view, cursor_x - w, cursor_y, w + cursor_w, h);
}

void print_line(const char *text)
{
  size_t text_len = strlen(text);
  flip_cursor();

  uint32_t w, h;
  ui_measure_text(&w, &h, text, text_len, UI_FONT_MONACO);

  ui_scrollview_grow(&view, cursor_x + w + cursor_w, cursor_y + h + line_height, view.window_h);
  ui_render_text(view.content_buf + (cursor_y * view.content_w) + cursor_x,
                 view.content_w,
                 text,
                 text_len,
                 UI_FONT_MONACO);

  cursor_y += h;
  uint32_t old_cursor_x = cursor_x;
  cursor_x = 0;
  flip_cursor();
  ui_scrollview_redraw_rect(&view, 0, cursor_y - h, old_cursor_x + w + cursor_w, h + line_height);
}

void print_prompt()
{
  char buf[SMALL_BUFFER_SIZE];
  getcwd(buf, SMALL_BUFFER_SIZE - 4);
  size_t len = strlen(buf);
  memcpy(buf + len, " % ", 4);
  print(buf);
}

bool is_path_executable(const char *p)
{
  struct stat st;
  int32_t res = stat(p, &st);
  if (res)
    return 0;
  if ((st.st_dev & 1) == 0)
    return 0;
  return 1;
}

bool find_path(char *out, char *prog_name, char *env_path)
{
  char *path = strdup(env_path);
  size_t path_len = strlen(path);
  size_t prog_name_len = strlen(prog_name);

  for (unsigned i = 0; i < path_len; ++i)
    if (path[i] == ':')
      path[i] = 0;

  size_t step = strlen(path);
  for (uint32_t i = 0; i < path_len; i += step + 1) {
    step = strlen(path + i);

    // FIXME should do bounds checking here
    char this_path[SMALL_BUFFER_SIZE];
    memcpy(this_path, path + i, step);
    this_path[step] = '/';
    memcpy(this_path + step + 1, prog_name, prog_name_len);
    this_path[step + 1 + prog_name_len] = '\0';

    if (is_path_executable(this_path)) {
      strncpy(out, this_path, SMALL_BUFFER_SIZE);
      free(path);
      return true;
    }
  }

  free(path);
  return false;
}

void program_thread()
{
  close(prog_write_fd);
  while (1) {
    char buf[SMALL_BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    int32_t r = read(prog_read_fd, buf, SMALL_BUFFER_SIZE - 1);
    if (r <= 0)
      break;

    thread_lock(&ui_lock);
    size_t buf_len = strlen(buf);
    char *printp = buf;
    for (size_t i = 0; i < buf_len; ++i) {
      if (buf[i] == '\n') {
        buf[i] = '\0';
        print_line(printp);
        printp = buf + i + 1;
      }
    }
    print(printp);
    thread_unlock(&ui_lock);
  }

  thread_lock(&ui_lock);
  close(prog_read_fd);
  prog_read_fd = 0;
  executing_program = false;
  print_prompt();
  thread_unlock(&ui_lock);
}

void execute_async(const char *prog, char **args)
{
  executing_program = true;

  uint32_t readfd, writefd;
  int32_t err = pipe(&readfd, &writefd);
  if (err)
    return;

  uint32_t readfd2, writefd2;
  err = pipe(&readfd2, &writefd2);
  if (err)
    return;

  prog_pid = fork();
  if (prog_pid == 0) {
    close(readfd);
    close(writefd2);

    uint32_t errfd = dup(writefd);
    movefd(readfd2, 0);
    movefd(writefd, 1);
    movefd(errfd, 2);

    execve(prog, args, environ);
    exit(1);
  }

  prog_read_fd = readfd;
  prog_write_fd = writefd2;
  close(writefd);
  close(readfd2);
  thread(program_thread, NULL);
  close(readfd);
}

void execute_program(char *cmd_buf, size_t cmd_len)
{
  for (unsigned i = 0; i < cmd_len; ++i)
    if (cmd_buf[i] == ' ')
      cmd_buf[i] = '\0';

  if (cmd_buf[0] == '\0') {
    print_prompt();
    return;
  }

  char *args[SMALL_BUFFER_SIZE];
  unsigned args_idx = 0;
  size_t cur_arg_len = 0;
  for (unsigned i = strlen(cmd_buf) + 1; i < cmd_len; i += cur_arg_len + 1) {
    cur_arg_len = strlen(cmd_buf + i);
    if (cur_arg_len == 0)
      continue;
    args[args_idx] = cmd_buf + i;
    ++args_idx;
    if (args_idx >= SMALL_BUFFER_SIZE - 1)
      break;
  }
  args[args_idx] = NULL;

  char prog_path[SMALL_BUFFER_SIZE];

  if (find_path(prog_path, cmd_buf, getenv("APPS_PATH"))) {
    if (fork() == 0) {
      execve(prog_path, args, environ);
      exit(1);
    }
    print_prompt();
    return;
  }

  if (find_path(prog_path, cmd_buf, getenv("PATH"))) {
    execute_async(prog_path, args);
    return;
  }

  if (is_path_executable(cmd_buf)) {
    execute_async(cmd_buf, args);
    return;
  }

  char out[SMALL_BUFFER_SIZE];
  snprintf(out, SMALL_BUFFER_SIZE, "'%s' not found", cmd_buf);
  print_line(out);
  print_prompt();
}

void keyboard_handler(uint8_t code)
{
  static bool lshift = false;
  static bool rshift = false;

  thread_lock(&ui_lock);

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
    thread_unlock(&ui_lock);
    return;
  }

  switch (code) {
    case KB_SC_LSHIFT:
      lshift = true;
      break;
    case KB_SC_RSHIFT:
      rshift = true;
      break;
    case KB_SC_UP:
      ui_scrollview_scroll(&view, 0, -8);
      break;
    case KB_SC_DOWN:
      ui_scrollview_scroll(&view, 0, 8);
      break;
    case KB_SC_ENTER: {
      size_t input_len = line_idx;
      char input[input_len + 1];
      memcpy(input, line_buf, input_len + 1);
      memset(line_buf, 0, sizeof(line_buf));
      line_idx = 0;

      char tmp = 0;
      print_line(&tmp);

      if (!executing_program) {
        if (prog_write_fd != 0) {
          close(prog_write_fd);
          prog_write_fd = 0;
        }
        execute_program(input, input_len);
      } else {
        input[input_len] = '\n';
        write(prog_write_fd, input, input_len + 1);
      }
      break;
    }
    case KB_SC_BS: {
      if (line_idx == 0)
        break;

      --line_idx;
      char deleted = line_buf[line_idx];
      line_buf[line_idx] = 0;
      uint32_t w, h;
      ui_measure_text(&w, &h, &deleted, 1, UI_FONT_MONACO);
      cursor_x -= w;
      fill_rect(cursor_x, cursor_y, w + cursor_w, line_height, background_color);
      flip_cursor();
      ui_scrollview_redraw_rect(&view, cursor_x, cursor_y, w + cursor_w, h);
      break;
    }
    default: {
      char c = scancode_to_ascii(code, lshift || rshift);
      if (c != 0) {
        line_buf[line_idx] = c;
        ++line_idx;
        char buf[] = { c, 0 };
        print(buf);
      }
    }
  }

  thread_unlock(&ui_lock);
}

void resize_request_handler(uint32_t w, uint32_t h)
{
  thread_lock(&ui_lock);
  uint32_t *new_ui_buf = malloc(w * h * sizeof(uint32_t));
  if (new_ui_buf == NULL) {
    thread_unlock(&ui_lock);
    return;
  }

  free(view.content_buf);
  if (!ui_scrollview_init(&view, new_ui_buf, w, h)) {
    thread_unlock(&ui_lock);
    return;
  }

  cursor_x = 0;
  cursor_y = 0;
  line_idx = 0;
  memset(line_buf, 0, sizeof(line_buf));

  if (!executing_program)
    print_prompt();

  int32_t err = ui_resize_window(new_ui_buf, w, h);
  if (err) {
    thread_unlock(&ui_lock);
    return;
  }

  free(ui_buf);
  ui_buf = new_ui_buf;
  thread_unlock(&ui_lock);
}

int main(int argc, char *argv[])
{
  priority(1);

  memset(line_buf, 0, sizeof(line_buf));

  if (argc > 1)
    chdir(argv[1]);

  ui_buf = malloc((SCREENWIDTH >> 1) * (SCREENHEIGHT >> 1) * sizeof(uint32_t));
  int32_t err = ui_acquire_window(ui_buf, "term", SCREENWIDTH >> 1, SCREENHEIGHT >> 1);
  if (err < 0)
    return 1;

  ui_event_t ev;
  err = ui_next_event(&ev);
  if (err < 0 || ev.type != UI_EVENT_WAKE)
    return 1;

  if (!ui_scrollview_init(&view, ui_buf, ev.width, ev.height))
    return 1;

  print_prompt();

  while (1) {
    ui_event_t ev;
    err = ui_next_event(&ev);
    if (err < 0)
      return 1;
    switch (ev.type) {
      case UI_EVENT_KEYBOARD:
        keyboard_handler(ev.code);
        break;
      case UI_EVENT_SCROLL:
        thread_lock(&ui_lock);
        ui_scrollview_scroll(&view, ev.hscroll * 10, ev.vscroll * 10);
        thread_unlock(&ui_lock);
        break;
      case UI_EVENT_RESIZE_REQUEST:
        resize_request_handler(ev.width, ev.height);
        break;
      default:;
    }
  }

  return 0;
}
