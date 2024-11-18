
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "process.h"
#include "kheap.h"
#include "klock.h"
#include "ds.h"
#include "util.h"
#include "paging.h"
#include "interrupt.h"
#include "log.h"
#include "ui.h"
#include "pipe.h"
#include "fs.h"
#include "ui_cursor.h"
#include "ui_title_bar.h"

#define CHECK(err, msg, code)                                                  \
  if ((err)) {                                                                 \
    log_error("ui", msg "\n");                                                 \
    return (code);                                                             \
  }
#define CHECK_UNLOCK_R(err, msg, code)                                         \
  if ((err)) {                                                                 \
    log_error("ui", msg "\n");                                                 \
    kunlock(&responders_lock);                                                 \
    return (code);                                                             \
  }
#define CHECK_RESTORE_EFLAGS(err, msg, code)                                   \
  if ((err)) {                                                                 \
    log_error("ui", msg "\n");                                                 \
    interrupt_restore(eflags);                                                 \
    return (code);                                                             \
  }

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

struct point {
  int32_t x;
  int32_t y;
};

struct dim {
  uint32_t w;
  uint32_t h;
};

// FIXME Strange bug: Create two instances of an app (e.g. launch
// another pie from pie on boot-up) and drag both to the upper edge
// of the screen such that their title bars are above the screen.
// Click close button on either and observe page fault.

// FIXME make a pixel_buffer type with uint32_t* and line stride

typedef struct ui_responder_s {
  process_t *process;
  ui_window_t window;
  uint8_t window_opacity;
  uint8_t window_is_moving;
  uint32_t *buf;
  fs_node_t event_pipe_read;
  fs_node_t event_pipe_write;
  list_node_t *list_node;
} ui_responder_t;

static const size_t frame_size = SCREENWIDTH * SCREENHEIGHT * sizeof(uint32_t);
static uint32_t *wallpaper = NULL;
static uint32_t *static_objects = NULL;
static uint32_t *moving_objects = NULL;
static uint32_t *frame_buffer = 0;

// FIXME use a struct point for mouse pos
static int32_t mouse_x = 100;
static int32_t mouse_y = 100;
static uint8_t mouse_left_clicked = 0;

static list_t responders;
static volatile uint32_t responders_lock = 0;

static ui_responder_t *responders_by_gid[MAX_PROCESS_COUNT];

static inline uint32_t blend_alpha(uint32_t bg, uint32_t fg, uint8_t opacity)
{
  const uint8_t fg_b = ((fg & 0xff) * opacity) / 0xff;
  const uint8_t fg_g = (((fg >> 8) & 0xff) * opacity) / 0xff;
  const uint8_t fg_r = (((fg >> 16) & 0xff) * opacity) / 0xff;
  const uint32_t bg_b = bg & 0xff;
  const uint32_t bg_g = (bg >> 8) & 0xff;
  const uint32_t bg_r = (bg >> 16) & 0xff;

  const uint16_t t = 0xff ^ opacity;
  const uint32_t blend_g = fg_g + (((bg_g * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_b = fg_b + (((bg_b * t + 0x80) * 0x101) >> 16);
  const uint32_t blend_r = fg_r + (((bg_r * t + 0x80) * 0x101) >> 16);

  return blend_b | (blend_g << 8) | (blend_r << 16);
}

static void draw_rect(
  uint32_t *dst, uint32_t dst_stride, struct point dst_point,
  const uint32_t *src, uint32_t src_stride, struct point src_point,
  struct dim dim, uint8_t opacity
)
{
  for (uint32_t y = 0; y < dim.h; ++y) {
    const uint32_t dst_offset = (dst_point.y + y) * dst_stride + dst_point.x;
    const uint32_t src_offset = (src_point.y + y) * src_stride + src_point.x;
    if (opacity == 0xff)
      u_memcpy(dst + dst_offset, src + src_offset, dim.w * sizeof(uint32_t));
    else
      for (uint32_t x = 0; x < dim.w; ++x)
        dst[dst_offset + x] = blend_alpha(dst[dst_offset + x], src[src_offset + x], opacity);
  }
}

// Blit a single window to a buffer.
static void ui_blit_window(ui_responder_t *r, uint32_t *buffer)
{
  const process_t *p = r->process;
  const uint32_t eflags = interrupt_save_disable();
  const uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  const struct point title_bar_dst = {
    .x = max(r->window.x, 0),
    .y = max(r->window.y - TITLE_BAR_HEIGHT, 0),
  };
  const struct point title_bar_src = {
    .x = title_bar_dst.x - r->window.x,
    .y = title_bar_dst.y - (r->window.y - TITLE_BAR_HEIGHT),
  };
  const struct dim title_bar_dim = {
    .w = min(r->window.x + TITLE_BAR_WIDTH, SCREENWIDTH) - title_bar_dst.x,
    .h = min(r->window.y, SCREENHEIGHT) - title_bar_dst.y,
  };

  draw_rect(buffer, SCREENWIDTH, title_bar_dst, TITLE_BAR_PIXELS, TITLE_BAR_WIDTH, title_bar_src,
            title_bar_dim, r->window_opacity);

  const struct point window_dst = {
    .x = max(r->window.x, 0),
    .y = max(r->window.y, 0),
  };
  const struct point window_src = {
    .x = window_dst.x - r->window.x,
    .y = window_dst.y - r->window.y,
  };
  const struct dim window_dim = {
    .w = min(r->window.x + r->window.w, SCREENWIDTH) - window_dst.x,
    .h = min(r->window.y + r->window.h, SCREENHEIGHT) - window_dst.y,
  };

  draw_rect(buffer, SCREENWIDTH, window_dst, r->buf, r->window.w, window_src,
            window_dim, r->window_opacity);

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
}

static void ui_blit_cursor()
{
  for (uint32_t y = 0; y < CURSOR_HEIGHT; ++y) {
    if (mouse_y + y >= SCREENHEIGHT) break;
    for (uint32_t x = 0; x < CURSOR_WIDTH; ++x) {
      if (mouse_x + x >= SCREENWIDTH) break;
      const uint32_t pixel_offset = ((mouse_y + y) * SCREENWIDTH) + mouse_x + x;
      const uint32_t cursor_pixel = CURSOR_PIXELS[y * CURSOR_WIDTH + x];
      // Only draw fully opaque pixels
      if (cursor_pixel & 0xff000000)
        moving_objects[pixel_offset] = cursor_pixel;
    }
  }
}

static void ui_redraw_key_responder()
{
  uint32_t eflags = interrupt_save_disable();
  ui_responder_t *r = responders.head->value;

  if (r->window_is_moving) ui_blit_window(r, moving_objects);
  else ui_blit_window(r, static_objects);

  const struct point title_bar_pos = {
    .x = max(r->window.x, 0),
    .y = max(r->window.y - TITLE_BAR_HEIGHT, 0),
  };
  const struct dim title_bar_dim = {
    .w = min(r->window.x + r->window.w, SCREENWIDTH) - title_bar_pos.x,
    .h = min(r->window.y, SCREENHEIGHT) - title_bar_pos.y,
  };
  const struct point window_pos = {
    .x = max(r->window.x, 0),
    .y = max(r->window.y, 0),
  };
  const struct dim window_dim = {
    .w = min(r->window.x + r->window.w, SCREENWIDTH) - window_pos.x,
    .h = min(r->window.y + r->window.h, SCREENHEIGHT) - window_pos.y,
  };

  if (r->window_is_moving) ui_blit_window(r, moving_objects);
  else {
    ui_blit_window(r, static_objects);
    draw_rect(moving_objects, SCREENWIDTH, title_bar_pos, static_objects, SCREENWIDTH, title_bar_pos,
              title_bar_dim, 0xff);
    draw_rect(moving_objects, SCREENWIDTH, window_pos, static_objects, SCREENWIDTH, window_pos,
              window_dim, 0xff);
  }

  draw_rect(frame_buffer, SCREENWIDTH, title_bar_pos, moving_objects, SCREENWIDTH, title_bar_pos,
            title_bar_dim, 0xff);
  draw_rect(frame_buffer, SCREENWIDTH, window_pos, moving_objects, SCREENWIDTH, window_pos,
            window_dim, 0xff);

  interrupt_restore(eflags);
}

static void ui_redraw_moving_objects(struct point old, struct point new)
{
  uint32_t moved_object_w = CURSOR_WIDTH;
  uint32_t moved_object_h = CURSOR_HEIGHT;

  ui_responder_t *key_responder = NULL;
  if (responders.size) {
    key_responder = responders.head->value;
    if (key_responder->window_is_moving) {
      moved_object_w = key_responder->window.w;
      moved_object_h = key_responder->window.h + TITLE_BAR_HEIGHT;
    }
  }

  const struct point old_rect_pos = {
    .x = max(old.x, 0),
    .y = max(old.y, 0),
  };
  const struct dim old_rect_dim = {
    .w = min(old.x + moved_object_w, SCREENWIDTH) - old_rect_pos.x,
    .h = min(old.y + moved_object_h, SCREENHEIGHT) - old_rect_pos.y,
  };

  draw_rect(moving_objects, SCREENWIDTH, old_rect_pos, static_objects, SCREENWIDTH, old_rect_pos,
            old_rect_dim, 0xff);

  if (key_responder && key_responder->window_is_moving)
    ui_blit_window(key_responder, moving_objects);

  ui_blit_cursor();

  const int32_t min_x = min(old.x, new.x);
  const int32_t max_x = max(old.x, new.x);
  const int32_t min_y = min(old.y, new.y);
  const int32_t max_y = max(old.y, new.y);

  const struct point redraw_pos = {
    .x = max(min_x, 0),
    .y = max(min_y, 0),
  };
  const struct dim redraw_rect_dim = {
    .w = min(max_x + moved_object_w, SCREENWIDTH) - redraw_pos.x,
    .h = min(max_y + moved_object_h, SCREENHEIGHT) - redraw_pos.y,
  };

  draw_rect(frame_buffer, SCREENWIDTH, redraw_pos, moving_objects, SCREENWIDTH, redraw_pos,
            redraw_rect_dim, 0xff);
}

// Redraw the entire screen.
static void ui_redraw_all()
{
  const uint32_t eflags = interrupt_save_disable();

  u_memcpy(static_objects, wallpaper, frame_size);

  // Blit all but the key responder
  for (list_node_t *current = responders.tail; current != responders.head; current = current->prev) {
    ui_responder_t *responder = current->value;
    ui_blit_window(responder, static_objects);
  }

  if (responders.size) {
    ui_responder_t *key_responder = responders.head->value;
    if (key_responder->window_is_moving) {
      u_memcpy(moving_objects, static_objects, frame_size);
      ui_blit_window(key_responder, moving_objects);
      ui_blit_cursor();
      u_memcpy(frame_buffer, moving_objects, frame_size);
    } else {
      ui_blit_window(key_responder, static_objects);
      u_memcpy(moving_objects, static_objects, frame_size);
      ui_blit_cursor();
      u_memcpy(frame_buffer, moving_objects, frame_size);
    }
  } else {
    u_memcpy(moving_objects, static_objects, frame_size);
    ui_blit_cursor();
    u_memcpy(frame_buffer, moving_objects, frame_size);
  }

  interrupt_restore(eflags);
}

uint32_t ui_init(uint32_t video_vaddr)
{
  u_memset(responders_by_gid, 0, sizeof(responders_by_gid));
  u_memset(&responders, 0, sizeof(list_t));
  frame_buffer = (uint32_t *)video_vaddr;

  // FIXME these should be page allocations and not kmallocs
  static_objects = kmalloc(frame_size);
  CHECK(static_objects == NULL, "Failed to allocate static_objects", ENOMEM);

  moving_objects = kmalloc(frame_size);
  CHECK(moving_objects == NULL, "Failed to allocate moving_objects", ENOMEM);

  wallpaper = kmalloc(frame_size);
  CHECK(wallpaper == NULL, "Failed to allocate wallpaper", ENOMEM);

  fs_node_t wallpaper_dir;
  uint32_t err = fs_open_node(&wallpaper_dir, "/wallpapers", O_DIRECTORY);
  CHECK(err, "Failed to open /wallpapers", ENOENT);

  struct dirent *wallpaper_dirent = fs_readdir(&wallpaper_dir, 2);
  CHECK(wallpaper_dirent == NULL, "Failed to read entry in /wallpapers", ENOTDIR);

  const size_t path_strlen = u_strlen("/wallpapers/");
  const size_t dirent_strlen = u_strlen(wallpaper_dirent->name);
  char name_buf[path_strlen + dirent_strlen + 1];
  u_memcpy(name_buf, "/wallpapers/", path_strlen);
  u_memcpy(name_buf + path_strlen, wallpaper_dirent->name, dirent_strlen + 1);
  err = ui_set_wallpaper(name_buf);
  CHECK(err, "Failed to set wallpaper", err);
  kfree(wallpaper_dirent);

  return 0;
}

static uint32_t ui_dispatch_window_event(ui_responder_t *r, ui_event_type_t t)
{
  ui_event_t ev;
  ev.type = t;
  ev.width = r->window.w;
  ev.height = r->window.h;

  uint32_t written = fs_write(&r->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);

  if (written != sizeof(ui_event_t)) return -1;
  return 0;
}

uint32_t ui_handle_keyboard_event(uint8_t code)
{
  uint32_t eflags = interrupt_save_disable();

  if (responders.size == 0) {
    interrupt_restore(eflags);
    return 0;
  }

  static const uint8_t scancode_meta_pressed = 0x38;
  static const uint8_t scancode_meta_released = 0xb8;
  static const uint8_t scancode_tab_pressed = 0x0f;
  static uint8_t meta_pressed = 0;

  if (meta_pressed && code == scancode_tab_pressed) {
    // Rotate responders list
    if (responders.size > 1) {
      ui_responder_t *key_responder = responders.head->value;
      list_remove(&responders, responders.head, 0);
      key_responder->list_node->prev = responders.tail;
      key_responder->list_node->next = NULL;
      responders.tail->next = key_responder->list_node;
      responders.tail = key_responder->list_node;
      responders.size++;

      uint32_t err = ui_dispatch_window_event(key_responder, UI_EVENT_SLEEP);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch sleep event.", err);
      err = ui_dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch wake event.", err);

      ui_redraw_all();
    }

    interrupt_restore(eflags);
    return 0;
  }

  if (code == scancode_meta_pressed)
    meta_pressed = 1;
  else if (code == scancode_meta_released)
    meta_pressed = 0;

  ui_event_t ev;
  ev.type = UI_EVENT_KEYBOARD;
  ev.code = code;

  ui_responder_t *key_responder = responders.head->value;
  uint32_t written = fs_write(&key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);

  interrupt_restore(eflags);
  if (written != sizeof(ui_event_t)) return -1;
  return 0;
}

static inline uint8_t mouse_in_rect(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
  return mouse_x >= x && mouse_x < x + (int32_t)w && mouse_y >= y && mouse_y < y + (int32_t)h;
}

static void ui_handle_mouse_click()
{
  ui_responder_t *new_key_responder = NULL;
  uint8_t changed_opacity = 0;
  list_foreach(node, &responders) {
    ui_responder_t *r = node->value;
    if (mouse_in_rect(r->window.x, r->window.y - TITLE_BAR_HEIGHT, TITLE_BAR_BUTTON_WIDTH, TITLE_BAR_HEIGHT)) {
      process_kill(r->process);
      return;
    }
    if (mouse_in_rect(r->window.x + TITLE_BAR_WIDTH - TITLE_BAR_BUTTON_WIDTH,
                      r->window.y - TITLE_BAR_HEIGHT, TITLE_BAR_BUTTON_WIDTH, TITLE_BAR_HEIGHT)) {
      new_key_responder = r;
      if (r->window_opacity == 0x33) r->window_opacity = 0xff;
      else r->window_opacity -= 0x44;
      changed_opacity = 1;
      break;
    }
    if (mouse_in_rect(r->window.x, r->window.y - TITLE_BAR_HEIGHT, TITLE_BAR_WIDTH, TITLE_BAR_HEIGHT)) {
      new_key_responder = r;
      r->window_is_moving = 1;
      break;
    }
    if (mouse_in_rect(r->window.x, r->window.y, r->window.w, r->window.h)) {
      new_key_responder = r;
      break;
    }
  }

  if (new_key_responder == NULL) return;
  if (new_key_responder == responders.head->value && !new_key_responder->window_is_moving
      && !changed_opacity)
    return;

  if (new_key_responder != responders.head->value) {
    list_remove(&responders, new_key_responder->list_node, 0);
    new_key_responder->list_node->next = responders.head;
    new_key_responder->list_node->prev = NULL;
    responders.head->prev = new_key_responder->list_node;
    responders.head = new_key_responder->list_node;
    responders.size++;
  }

  ui_redraw_all();
}

uint32_t ui_handle_mouse_event(int32_t dx, int32_t dy, uint8_t left_button, uint8_t right_button)
{
  uint8_t click_event = mouse_left_clicked != left_button;
  if (dx == 0 && dy == 0 && !click_event) return 0;

  mouse_left_clicked = left_button;

  ui_responder_t *key_responder = NULL;
  if (responders.size)
    key_responder = responders.head->value;

  if (click_event) {
    if (mouse_left_clicked) ui_handle_mouse_click();
    else if (key_responder && key_responder->window_is_moving) {
      key_responder->window_is_moving = 0;
      ui_redraw_all();
    }
    return 0;
  }

  const uint32_t old_mouse_x = mouse_x;
  const uint32_t old_mouse_y = mouse_y;

  mouse_x += dx;
  mouse_y -= dy;
  mouse_x = max(mouse_x, 0);
  mouse_x = min(mouse_x, SCREENWIDTH - 1);
  mouse_y = max(mouse_y, 0);
  mouse_y = min(mouse_y, SCREENHEIGHT - 1);

  if (key_responder && key_responder->window_is_moving) {
    const uint32_t old_window_x = key_responder->window.x;
    const uint32_t old_window_y = key_responder->window.y;
    key_responder->window.x += mouse_x - old_mouse_x;
    key_responder->window.y += mouse_y - old_mouse_y;
    struct point old = { .x = old_window_x, .y = old_window_y - TITLE_BAR_HEIGHT };
    struct point new = { .x = key_responder->window.x, .y = key_responder->window.y - TITLE_BAR_HEIGHT };
    ui_redraw_moving_objects(old, new);
    return 0;
  }

  struct point old = { .x = old_mouse_x, .y = old_mouse_y };
  struct point new = { .x = mouse_x, .y = mouse_y };
  ui_redraw_moving_objects(old, new);

  return 0;
}

uint32_t ui_make_responder(process_t *p, uint32_t buf)
{
  if (responders_by_gid[p->gid]) return 1;
  ui_responder_t *r = kmalloc(sizeof(ui_responder_t)); CHECK(r == NULL, "No memory.", ENOMEM);
  u_memset(r, 0, sizeof(ui_responder_t));
  uint32_t err = pipe_create(&r->event_pipe_read, &r->event_pipe_write);
  CHECK(err, "Failed to create event pipe.", err);
  r->process = p;
  r->buf = (uint32_t *)buf;
  r->window.w = SCREENWIDTH >> 1;
  r->window.h = SCREENHEIGHT >> 1;
  r->window_opacity = 0xff;

  klock(&responders_lock);
  r->window.x = SCREENWIDTH >> 2;
  r->window.y = SCREENHEIGHT >> 2;

  list_push_front(&responders, r);
  r->list_node = responders.head;
  responders_by_gid[p->gid] = r;
  p->has_ui = 1;
  err = ui_dispatch_window_event(r, UI_EVENT_WAKE);
  CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  if (responders.head->next) {
    err = ui_dispatch_window_event(responders.head->next->value, UI_EVENT_SLEEP);
    CHECK_UNLOCK_R(err, "Failed to dispatch sleep event.", err);
  }
  kunlock(&responders_lock);
  return 0;
}

uint32_t ui_kill(process_t *p)
{
  klock(&responders_lock);
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { kunlock(&responders_lock); return 0; }

  responders_by_gid[p->gid] = NULL;
  fs_close(&r->event_pipe_read);
  fs_close(&r->event_pipe_write);
  uint8_t is_head = responders.head->value == r;
  list_remove(&responders, r->list_node, 1);

  if (is_head && responders.size) {
    uint32_t err = ui_dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
    CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  }

  kunlock(&responders_lock);
  ui_redraw_all();
  return 0;
}

uint32_t ui_swap_buffers(process_t *p)
{
  uint32_t eflags = interrupt_save_disable();
  ui_responder_t *r = responders_by_gid[p->gid];
  CHECK_RESTORE_EFLAGS(r == NULL, "No responders available", 1);

  if (r == responders.head->value && r->window_opacity == 0xff)
    ui_redraw_key_responder();
  else ui_redraw_all();

  interrupt_restore(eflags);
  return 0;
}

uint32_t ui_yield(process_t *p)
{
  klock(&responders_lock);
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { kunlock(&responders_lock); return 1; }

  if (responders.head->value != r || responders.size <= 1) {
    kunlock(&responders_lock);
    return 0;
  }

  // Rotate responders list
  list_remove(&responders, responders.head, 0);
  r->list_node->prev = responders.tail;
  r->list_node->next = NULL;
  responders.tail->next = r->list_node;
  responders.tail = r->list_node;
  responders.size++;

  uint32_t err = ui_dispatch_window_event(r, UI_EVENT_SLEEP);
  CHECK_UNLOCK_R(err, "Failed to dispatch sleep event.", err);
  err = ui_dispatch_window_event(responders.head->value, UI_EVENT_WAKE);
  CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);

  kunlock(&responders_lock);

  ui_redraw_all();
  return 0;
}

uint32_t ui_next_event(process_t *p, uint32_t buf)
{
  klock(&responders_lock);
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { kunlock(&responders_lock); return 1; }
  // Do not hold responders lock while blocking on event_pipe_read
  kunlock(&responders_lock);

  uint8_t ev_buf[sizeof(ui_event_t)];
  uint32_t read_size = fs_read(&r->event_pipe_read, 0, sizeof(ui_event_t), ev_buf);
  if (read_size < sizeof(ui_event_t)) return 1;

  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);
  u_memcpy((uint8_t *)buf, ev_buf, sizeof(ui_event_t));
  paging_set_cr3(cr3);
  interrupt_restore(eflags);

  return 0;
}

uint32_t ui_poll_events(process_t *p)
{
  klock(&responders_lock);
  ui_responder_t *r = responders_by_gid[p->gid];
  uint32_t count = 0;
  if (r != NULL)
    count = r->event_pipe_read.length / sizeof(ui_event_t);
  kunlock(&responders_lock);
  return count;
}

uint32_t ui_set_wallpaper(const char *path)
{
  uint32_t eflags = interrupt_save_disable();
  fs_node_t wallpaper_node;
  uint32_t err = fs_open_node(&wallpaper_node, path, 0);
  CHECK_RESTORE_EFLAGS(err, "Failed to open wallpaper file", err);

  uint32_t n = fs_read(&wallpaper_node, 0, wallpaper_node.length, (uint8_t *)wallpaper);
  CHECK_RESTORE_EFLAGS(n != frame_size, "Failed to read wallpaper file", n);

  ui_redraw_all();
  interrupt_restore(eflags);
  return 0;
}
