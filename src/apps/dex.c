
// dex.c
//
// Directory EXplorer.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/scancode.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <mako.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ui.h>
#include <unistd.h>

// FIXME remove these
#define FONTWIDTH 7

#define PATH_HEIGHT 24
#define FOOTER_HEIGHT 24
#define DIRENT_HEIGHT 20
#define MAX_DIRENTS ((SCREENHEIGHT - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT)
#define FOOTER_LEN (SCREENWIDTH / FONTWIDTH)

static const uint32_t BG_COLOR = 0xffffeb;
static const uint32_t INACTIVE_BG_COLOR = 0xffffff;
static const uint32_t INACTIVE_COLOR = 0xb0b0b0;
static const uint32_t CURSOR_COLOR = 0xb43a3b;
static const uint32_t CURSOR_WIDTH = 30;

static uint32_t *ui_buf = NULL;
static uint32_t window_w = 0;
static uint32_t window_h = 0;
static volatile uint32_t ui_lock = 0;

static char *current_path = NULL;
static uint32_t cursor_idx = 0;
static uint32_t top_idx = 0;
static struct dirent dirents[MAX_DIRENTS];
static char *file_path = NULL;
static char *exec_path = NULL;
static pid_t exec_pid = 0;

typedef enum
{
  CS_DEFAULT,
  CS_CREATE_TYPE,
  CS_CREATE_NAME,
  CS_EDIT_NAME,
  CS_EXEC_STDIN,
  CS_EXEC_PENDING
} command_state_t;

static command_state_t cs;
static char footer_text[FOOTER_LEN];

typedef enum
{
  ET_DIRECTORY,
  ET_FILE
} entry_type_t;

static entry_type_t create_entry_type;
static char create_entry_name[FOOTER_LEN];
static char edit_entry_name[FOOTER_LEN];
static char exec_stdin_path[FOOTER_LEN];

__attribute__((always_inline)) static inline void fill_color(uint32_t *p, uint32_t b, size_t n)
{
  for (uint32_t i = 0; i < n; ++i)
    p[i] = b;
}

static void update_footer_text();

static void render_text(const char *text, uint32_t x, uint32_t y)
{
  size_t len = strlen(text);
  uint32_t *p = ui_buf + (y * window_w) + x;
  ui_render_text(p, window_w, text, len, UI_FONT_MONACO, 0);
}

static void render_path()
{
  fill_color(ui_buf, BG_COLOR, window_w * PATH_HEIGHT);
  render_text(current_path, 4, 4);
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

static void load_dirents()
{
  uint32_t num_dirents = (window_h - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT;
  if (num_dirents > MAX_DIRENTS)
    num_dirents = MAX_DIRENTS;

  DIR *d = opendir(current_path);
  while (d == NULL) {
    char *npath = strdup(dirname(current_path));
    free(current_path);
    current_path = npath;
    d = opendir(current_path);
  }

  memset(dirents, 0, sizeof(dirents));

  uint32_t current_idx = 0;
  for (; current_idx < top_idx; ++current_idx) {
    struct dirent *ent = readdir(d);
    if (ent == NULL) {
      closedir(d);
      return;
    }
  }

  for (current_idx = 0; current_idx < num_dirents; ++current_idx) {
    struct dirent *ent = readdir(d);
    if (ent == NULL)
      break;
    dirents[current_idx] = *ent;
    free(ent);
  }

  closedir(d);
}

static void render_dirents()
{
  uint32_t num_dirents = (window_h - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT;
  if (num_dirents > MAX_DIRENTS)
    num_dirents = MAX_DIRENTS;

  fill_color(ui_buf + (window_w * (PATH_HEIGHT + 1)),
             BG_COLOR,
             window_w * (window_h - PATH_HEIGHT - FOOTER_HEIGHT - 1));

  uint32_t y = PATH_HEIGHT;
  for (uint32_t i = 0; i < num_dirents; ++i) {
    if (dirents[i].d_name[0] == 0)
      break;
    render_text(dirents[i].d_name, CURSOR_WIDTH, y + 4);
    y += DIRENT_HEIGHT;
  }
}

__attribute__((always_inline)) static inline void render_inactive()
{
  for (uint32_t i = 0; i < window_w; ++i)
    for (uint32_t j = 0; j < window_h; ++j)
      ui_buf[(j * window_w) + i] =
        ui_buf[(j * window_w) + i] == BG_COLOR ? INACTIVE_BG_COLOR : INACTIVE_COLOR;
}

static void update_cursor(uint32_t new_idx)
{
  uint32_t num_dirents = (window_h - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT;
  if (num_dirents > MAX_DIRENTS)
    num_dirents = MAX_DIRENTS;
  if (cursor_idx < num_dirents) {
    uint32_t *row = ui_buf + (((cursor_idx * DIRENT_HEIGHT) + 1 + PATH_HEIGHT) * window_w);
    for (uint32_t i = 0; i < DIRENT_HEIGHT - 2; ++i) {
      fill_color(row, BG_COLOR, CURSOR_WIDTH);
      row += window_w;
    }
  }

  if (new_idx >= num_dirents)
    new_idx = num_dirents - 1;

  cursor_idx = new_idx;
  uint32_t *row = ui_buf + (((cursor_idx * DIRENT_HEIGHT) + 1 + PATH_HEIGHT) * window_w);
  for (int32_t i = 0; i < (DIRENT_HEIGHT - 8) / 2; ++i) {
    fill_color(row + 20, CURSOR_COLOR, i);
    row += window_w;
  }
  for (int32_t i = 0; i < (DIRENT_HEIGHT - 8) / 2; ++i) {
    fill_color(row + 20, CURSOR_COLOR, ((DIRENT_HEIGHT - 8) / 2) - i);
    row += window_w;
  }
}

static uint8_t scroll_up()
{
  if (top_idx == 0)
    return 0;
  --top_idx;
  load_dirents();
  return 1;
}

static uint8_t scroll_down()
{
  ++top_idx;
  load_dirents();
  while (dirents[cursor_idx].d_name[0] == 0)
    update_cursor(cursor_idx - 1);
  return 1;
}

static void duplicate_rec(char *srcdir, char *src, char *dstdir, char *dst)
{
  size_t sdirlen = strlen(srcdir);
  size_t srclen = strlen(src);
  size_t ddirlen = strlen(dstdir);
  size_t dstlen = strlen(dst);
  char *srcpath = malloc(sdirlen + srclen + 2);
  strcpy(srcpath, srcdir);
  if (srcdir[sdirlen - 1] != '/')
    srcpath[sdirlen] = '/';
  strcpy(srcpath + sdirlen + (srcdir[sdirlen - 1] != '/'), src);
  char *dstpath = malloc(ddirlen + dstlen + 2);
  strcpy(dstpath, dstdir);
  if (dstdir[ddirlen - 1] != '/')
    dstpath[ddirlen] = '/';
  strcpy(dstpath + ddirlen + (dstdir[ddirlen - 1] != '/'), dst);

  struct stat st;
  int32_t res = stat(srcpath, &st);
  if (res) {
  ret:
    free(srcpath);
    free(dstpath);
    return;
  }

  if ((st.st_dev & 2) == 0) {
    if ((st.st_dev & 0x20)) { // Symlink
      char *buf = calloc(1, MAXPATHLEN);
      size_t s = readlink(srcpath, buf, MAXPATHLEN);
      if (s == 0) {
        free(buf);
        goto ret;
      }
      symlink(buf, dstpath);
      free(buf);
      goto ret;
    }
    FILE *f = fopen(srcpath, "r");
    if (f == NULL)
      goto ret;
    char *buf = malloc(st.st_size);
    fread(buf, 1, st.st_size, f);
    fclose(f);
    f = fopen(dstpath, "w");
    fwrite(buf, 1, st.st_size, f);
    fclose(f);
    free(buf);
    goto ret;
  }

  DIR *ind = opendir(srcpath);
  if (ind == NULL)
    goto ret;
  res = mkdir(dstpath, st.st_mode);
  if (res)
    goto ret;
  DIR *outd = opendir(dstpath);
  if (outd == NULL)
    goto ret;

  struct dirent *ent = readdir(ind);
  uint32_t i = 0;
  for (; ent; ent = readdir(ind), ++i) {
    if (i < 2)
      continue;
    duplicate_rec(srcpath, ent->d_name, dstpath, ent->d_name);
    free(ent);
  }

  closedir(ind);
  closedir(outd);
  free(srcpath);
  free(dstpath);
}

static void duplication_thread(void *data)
{
  thread_lock(&ui_lock);
  char *start_dir = strdup(current_path);
  thread_unlock(&ui_lock);
  char *entname = data;
  size_t len = strlen(entname);
  char *dupname = malloc(len + 4);
  strcpy(dupname, entname);
  strcpy(dupname + len, "(1)");

  duplicate_rec(start_dir, entname, start_dir, dupname);

  thread_lock(&ui_lock);
  if (strcmp(current_path, start_dir)) {
    thread_unlock(&ui_lock);
    free(start_dir);
    free(entname);
    free(dupname);
    return;
  }

  load_dirents();
  render_dirents();
  update_cursor(cursor_idx);
  ui_redraw_rect(0, 0, window_w, window_h);
  thread_unlock(&ui_lock);
  free(start_dir);
  free(entname);
  free(dupname);
}

static void duplicate_entry()
{
  char *p = strdup(dirents[cursor_idx].d_name);
  if (strcmp(p, ".") == 0 || strcmp(p, "..") == 0) {
    free(p);
    return;
  }
  thread(duplication_thread, strdup(dirents[cursor_idx].d_name));
}

static uint8_t delete_entry()
{
  int32_t res = unlink(dirents[cursor_idx].d_name);
  if (res)
    return 0;
  load_dirents();
  return 1;
}

static uint8_t edit_entry()
{
  int32_t res = rename(dirents[cursor_idx].d_name, edit_entry_name);
  if (res == 0)
    load_dirents();
  return res == 0;
}

static uint8_t create_entry()
{
  int32_t fd = 0;
  if (create_entry_type == ET_DIRECTORY) {
    fd = mkdir(create_entry_name, 0660);
    if (fd == 0)
      load_dirents();
    return fd == 0;
  }
  fd = open(create_entry_name, O_CREAT, 0660);
  if (fd > 0) {
    load_dirents();
    close(fd);
  }
  return fd > 0;
}

static uint8_t open_dirent(uint32_t idx)
{
  uint32_t num_dirents = (window_h - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT;
  if (num_dirents > MAX_DIRENTS)
    num_dirents = MAX_DIRENTS;

  if (idx >= num_dirents)
    return 0;

  struct dirent ent = dirents[idx];
  if (ent.d_name[0] == 0)
    return 0;

  if (strcmp(ent.d_name, ".") == 0) {
    top_idx = 0;
    load_dirents();
    return 0;
  }

  if (strcmp(ent.d_name, "..") == 0) {
    char *npath = strdup(dirname(current_path));
    int32_t res = chdir(npath);
    if (res == -1) {
      free(npath);
      return 0;
    }
    free(current_path);
    current_path = npath;
    top_idx = 0;
    load_dirents();
    return 0;
  }

  struct stat st;
  int32_t res = stat(ent.d_name, &st);
  if (res == -1) {
    top_idx = 0;
    load_dirents();
    return 0;
  }

  size_t len = strlen(ent.d_name);
  size_t clen = strlen(current_path);
  char *npath = malloc(clen + len + 2);
  memset(npath, 0, clen + len + 2);
  strcpy(npath, current_path);
  if (clen > 1)
    npath[clen] = '/';
  strcat(npath, ent.d_name);

  if (st.st_dev & 2) { // Directory.
    int32_t res = chdir(npath);
    if (res == -1) {
      free(npath);
      return 0;
    }
    free(current_path);
    current_path = npath;
    top_idx = 0;
    load_dirents();
    return 0;
  }

  if ((st.st_dev & 1) == 0)
    return 0;

  free(file_path);
  file_path = npath;
  return 1;
}

static void exec_thread(void *data)
{
  int32_t t;
  waitpid(exec_pid, &t, 0);

  thread_lock(&ui_lock);
  free(file_path);
  file_path = data;
  cs = CS_DEFAULT;
  update_footer_text();
  render_footer();
  ui_redraw_rect(0, 0, window_w, window_h);
  thread_unlock(&ui_lock);
}

static uint8_t exec_entry()
{
  char *apps_path = getenv("APPS_PATH");
  size_t aplen = apps_path ? strlen(apps_path) : 0;
  if (apps_path && strncmp(current_path, apps_path, aplen) == 0) {
    free(exec_path);
    exec_path = strdup(dirents[cursor_idx].d_name);
    return 2;
  }

  char *path = strdup(dirents[cursor_idx].d_name);
  char *tmpf = calloc(1, 256);
  tmpnam(tmpf);
  int32_t ofd = open(tmpf, O_WRONLY | O_TRUNC);
  if (ofd == -1 && errno == ENOENT)
    ofd = open(tmpf, O_WRONLY | O_CREAT, 0666);
  if (ofd == -1) {
    free(path);
    free(tmpf);
    return 0;
  }

  pid_t p = fork();
  if (p == 0) {
    char *arg0 = NULL;
    if (strlen(exec_stdin_path)) {
      arg0 = "-";
      FILE *f = fopen(exec_stdin_path, "r");
      if (f)
        movefd(f->fd, 0);
    }

    movefd(ofd, 1);

    char *args[] = { arg0, NULL };
    execve(path, args, environ);
    printf("dex: error: %d\n", errno);
    exit(1);
  }

  exec_pid = p;
  thread(exec_thread, tmpf);

  return 1;
}

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

  if (cs == CS_DEFAULT) {
    uint8_t swap = 1;
    uint8_t update_cursor_delete = 0;
    uint32_t num_dirents = (window_h - PATH_HEIGHT - FOOTER_HEIGHT) / DIRENT_HEIGHT;
    if (num_dirents > MAX_DIRENTS)
      num_dirents = MAX_DIRENTS;
    switch (code) {
      case KB_SC_ENTER:
        if (!open_dirent(cursor_idx)) {
          render_path();
          render_dirents();
          update_cursor(0);
        }
        break;
      case KB_SC_N:
        cs = CS_CREATE_TYPE;
        update_footer_text();
        render_footer();
        break;
      case KB_SC_D:
        if (!delete_entry()) {
          swap = 0;
          break;
        }
        if (dirents[cursor_idx].d_name[0] == 0) {
          if (cursor_idx == 0)
            scroll_up();
          else
            update_cursor_delete = 1;
        }
        render_dirents();
        if (update_cursor_delete)
          update_cursor(cursor_idx - 1);
        else
          update_cursor(cursor_idx);
        break;
      case KB_SC_C:
        duplicate_entry();
        break;
      case KB_SC_E:
        cs = CS_EDIT_NAME;
        update_footer_text();
        render_footer();
        break;
      case KB_SC_X:
        cs = CS_EXEC_STDIN;
        update_footer_text();
        render_footer();
        break;
      case KB_SC_Q:
        exit(0);
      case KB_SC_UP:
        if (cursor_idx == 0) {
          if (scroll_up()) {
            render_dirents();
            update_cursor(cursor_idx);
          }
          break;
        }
        update_cursor(cursor_idx - 1);
        break;
      case KB_SC_DOWN:
        if (cursor_idx == num_dirents - 1) {
          if (scroll_down()) {
            render_dirents();
            update_cursor(cursor_idx);
          }
          break;
        }
        if (dirents[cursor_idx + 1].d_name[0] == 0)
          break;
        update_cursor(cursor_idx + 1);
        break;
      default:
        swap = 0;
    }
    if (swap)
      ui_redraw_rect(0, 0, window_w, window_h);
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_CREATE_TYPE) {
    uint8_t update = 1;
    uint8_t cancel = 0;
    switch (code) {
      case KB_SC_D:
        create_entry_type = ET_DIRECTORY;
        break;
      case KB_SC_F:
        create_entry_type = ET_FILE;
        break;
      case KB_SC_ESC:
        cancel = 1;
        break;
      default:
        update = 0;
    }
    if (cancel) {
      cs = CS_DEFAULT;
      update_footer_text();
      render_footer();
      ui_redraw_rect(0, 0, window_w, window_h);
    } else if (update) {
      cs = CS_CREATE_NAME;
      update_footer_text();
      render_footer();
      ui_redraw_rect(0, 0, window_w, window_h);
    }
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_CREATE_NAME) {
    uint8_t update = 1;
    uint8_t cancel = 0;
    uint8_t created = 0;
    size_t len = strlen(create_entry_name);
    char c = 0;
    switch (code) {
      case KB_SC_ESC:
        cancel = 1;
        break;
      case KB_SC_BS:
        if (len) {
          create_entry_name[len - 1] = '\0';
          update_footer_text();
          strcat(footer_text, create_entry_name);
        } else
          update = 0;
        break;
      case KB_SC_ENTER:
        if (len == 0) {
          update = 0;
          break;
        }
        created = create_entry();
        break;
      default:
        c = scancode_to_ascii(code, lshift || rshift || capslock);
        if (!c) {
          update = 0;
          break;
        }
        if (len + strlen(footer_text) >= FOOTER_LEN) {
          update = 0;
          break;
        }
        create_entry_name[len] = c;
        update_footer_text();
        strcat(footer_text, create_entry_name);
    }
    if (update) {
      if (cancel || created) {
        memset(create_entry_name, 0, sizeof(create_entry_name));
        cs = CS_DEFAULT;
        update_footer_text();
      }
      if (created) {
        render_dirents();
        update_cursor(cursor_idx);
      }
      render_footer();
      ui_redraw_rect(0, 0, window_w, window_h);
    }
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_EDIT_NAME) {
    uint8_t update = 1;
    uint8_t cancel = 0;
    uint8_t edited = 0;
    size_t len = strlen(edit_entry_name);
    char c = 0;
    switch (code) {
      case KB_SC_ESC:
        cancel = 1;
        break;
      case KB_SC_BS:
        if (len) {
          edit_entry_name[len - 1] = '\0';
          update_footer_text();
          strcat(footer_text, edit_entry_name);
        } else
          update = 0;
        break;
      case KB_SC_ENTER:
        if (len == 0) {
          update = 0;
          break;
        }
        edited = edit_entry();
        break;
      default:
        c = scancode_to_ascii(code, lshift || rshift || capslock);
        if (!c) {
          update = 0;
          break;
        }
        if (len + strlen(footer_text) >= FOOTER_LEN) {
          update = 0;
          break;
        }
        edit_entry_name[len] = c;
        update_footer_text();
        strcat(footer_text, edit_entry_name);
    }
    if (update) {
      if (cancel || edited) {
        memset(edit_entry_name, 0, sizeof(edit_entry_name));
        cs = CS_DEFAULT;
        update_footer_text();
      }
      if (edited) {
        render_dirents();
        update_cursor(cursor_idx);
      }
      render_footer();
      ui_redraw_rect(0, 0, window_w, window_h);
    }
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_EXEC_STDIN) {
    uint8_t update = 1;
    uint8_t cancel = 0;
    uint8_t execed = 0;
    size_t len = strlen(exec_stdin_path);
    char c = 0;
    switch (code) {
      case KB_SC_ESC:
        cancel = 1;
        break;
      case KB_SC_BS:
        if (len) {
          exec_stdin_path[len - 1] = '\0';
          update_footer_text();
          strcat(footer_text, exec_stdin_path);
        } else
          update = 0;
        break;
      case KB_SC_ENTER:
        execed = exec_entry();
        break;
      default:
        c = scancode_to_ascii(code, lshift || rshift || capslock);
        if (!c) {
          update = 0;
          break;
        }
        if (len + strlen(footer_text) >= FOOTER_LEN) {
          update = 0;
          break;
        }
        exec_stdin_path[len] = c;
        update_footer_text();
        strcat(footer_text, exec_stdin_path);
    }
    if (update) {
      if (cancel) {
        memset(edit_entry_name, 0, sizeof(edit_entry_name));
        cs = CS_DEFAULT;
        update_footer_text();
      }
      if (execed)
        memset(edit_entry_name, 0, sizeof(edit_entry_name));
      if (execed == 1) {
        cs = CS_EXEC_PENDING;
        update_footer_text();
      } else if (execed == 2) {
        cs = CS_DEFAULT;
        update_footer_text();
      }
      render_footer();
      ui_redraw_rect(0, 0, window_w, window_h);
    }
    thread_unlock(&ui_lock);
    return;
  }

  if (cs == CS_EXEC_PENDING) {
    switch (code) {
      case KB_SC_ESC:
        if (!exec_pid)
          break;
        signal_send(exec_pid, SIGKILL);
    }
    thread_unlock(&ui_lock);
  }
}

static void resize_handler(ui_event_t ev)
{
  thread_lock(&ui_lock);
  if (window_w == ev.width && window_h == ev.height) {
    thread_unlock(&ui_lock);
    return;
  }

  window_w = ev.width;
  window_h = ev.height;

  fill_color(ui_buf, BG_COLOR, window_w * window_h);
  load_dirents();
  render_path();
  render_footer();
  render_dirents();
  update_cursor(cursor_idx);

  ui_redraw_rect(0, 0, window_w, window_h);

  // FIXME this needs to be removed / reworked
  if (file_path) {
    if (fork() == 0) {
      char *args[] = { file_path, NULL };
      execve("/apps/xed", args, environ);
      exit(1);
    }
    free(file_path);
    file_path = NULL;
  } else if (exec_path) {
    if (fork() == 0) {
      char *args[] = { NULL };
      execve(exec_path, args, environ);
      exit(1);
    }
    free(exec_path);
    exec_path = NULL;
  }

  thread_unlock(&ui_lock);
}

static void update_footer_text()
{
  char *str = NULL;
  switch (cs) {
    case CS_DEFAULT:
      str = "[ENT]open | [x]exec | [n]new | [d]delete"
            " | [e]edit | [c]duplicate | [q]quit";
      break;
    case CS_CREATE_TYPE:
      str = "Entry type: [d]directory | [f]file | [ESC]cancel";
      break;
    case CS_EXEC_STDIN:
      str = "(Optional) `stdin' file path ([ESC]cancel): ";
      break;
    case CS_EXEC_PENDING:
      str = "[ESC]terminate";
      break;
    case CS_EDIT_NAME:
    case CS_CREATE_NAME:
      str = "Entry name ([ESC]cancel): ";
      break;
  }
  strncpy(footer_text, str, FOOTER_LEN);
}

int main(int argc, char *argv[])
{
  priority(1);

  if (argc > 1) {
    char buf[1024];
    resolve(buf, argv[1], 1024);
    current_path = strdup(buf);
  } else
    current_path = strdup("/");

  int32_t res = chdir(current_path);
  if (res == -1)
    return 1;

  memset(create_entry_name, 0, sizeof(create_entry_name));
  memset(edit_entry_name, 0, sizeof(edit_entry_name));
  memset(exec_stdin_path, 0, sizeof(exec_stdin_path));
  memset(dirents, 0, sizeof(dirents));
  cs = CS_DEFAULT;
  update_footer_text();

  ui_buf = malloc((SCREENWIDTH >> 1) * (SCREENHEIGHT >> 1) * sizeof(uint32_t));
  res = ui_acquire_window(ui_buf, "dex", SCREENWIDTH >> 1, SCREENHEIGHT >> 1);
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
