
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
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

#define BORDER_WIDTH     4
#define BORDER_COLOR_KEY 0x52aaad
#define BORDER_COLOR     0x9cefef

typedef struct ui_responder_s {
  process_t *process;
  ui_window_t window;
  uint32_t *buf;
  fs_node_t event_pipe_read;
  fs_node_t event_pipe_write;
  list_node_t *list_node;
} ui_responder_t;

static const size_t backbuf_size = SCREENWIDTH * SCREENHEIGHT * sizeof(uint32_t);
static uint32_t *wallpaper = NULL;
static uint32_t buf_vaddr = 0;
static uint32_t *backbuf = NULL;
static list_t *responders = NULL;
static volatile uint32_t responders_lock = 0;

static ui_responder_t *responders_by_gid[MAX_PROCESS_COUNT];

// Blit a single window to the backbuf.
static void ui_blit_window(ui_responder_t *r, uint8_t is_key)
{
  uint32_t border_color = is_key ? BORDER_COLOR_KEY : BORDER_COLOR;
  process_t *p = r->process;
  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  for (uint32_t y = 0; y < BORDER_WIDTH; ++y) {
    uint32_t *backbuf_ptr =
      backbuf + ((y + r->window.y - BORDER_WIDTH) * SCREENWIDTH) + r->window.x - BORDER_WIDTH;
    for (uint32_t x = 0; x < r->window.w + 2 * BORDER_WIDTH; ++x)
      backbuf_ptr[x] = border_color;
  }
  for (uint32_t y = 0; y < r->window.h; ++y) {
    uint32_t *r_buf_ptr = r->buf + (y * r->window.w);
    uint32_t *backbuf_ptr = backbuf + ((y + r->window.y) * SCREENWIDTH) + r->window.x;
    for (uint32_t x = 1; x <= BORDER_WIDTH; ++x) *(backbuf_ptr - x) = border_color;
    u_memcpy(backbuf_ptr, r_buf_ptr, r->window.w * 4);
    for (uint32_t x = 0; x < BORDER_WIDTH; ++x) backbuf_ptr[r->window.w + x] = border_color;
  }
  for (uint32_t y = 0; y < BORDER_WIDTH; ++y) {
    uint32_t *backbuf_ptr =
      backbuf + ((y + r->window.y + r->window.h) * SCREENWIDTH) + r->window.x - BORDER_WIDTH;
    for (uint32_t x = 0; x < r->window.w + 2 * BORDER_WIDTH; ++x)
      backbuf_ptr[x] = border_color;
  }

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
}

// Redraw the entire screen.
static void ui_redraw_all()
{
  uint32_t eflags = interrupt_save_disable();

  u_memcpy(backbuf, wallpaper, backbuf_size);

  for (list_node_t *current = responders->tail; current; current = current->prev) {
    ui_responder_t *responder = current->value;
    ui_blit_window(responder, current == responders->head);
  }

  u_memcpy((uint32_t *)buf_vaddr, backbuf, backbuf_size);
  interrupt_restore(eflags);
}

uint32_t ui_init(uint32_t vaddr)
{
  u_memset(responders_by_gid, 0, sizeof(responders_by_gid));
  buf_vaddr = vaddr;
  backbuf = kmalloc(backbuf_size);
  CHECK(backbuf == NULL, "Failed to allocate backbuf", ENOMEM);
  responders = kmalloc(sizeof(list_t));
  CHECK(responders == NULL, "Failed to allocate responders", ENOMEM);
  u_memset(responders, 0, sizeof(list_t));

  wallpaper = kmalloc(backbuf_size);
  CHECK(wallpaper == NULL, "Failed to allocate wallpaper", ENOMEM);
  fs_node_t wallpaper_node;
  uint32_t err = fs_open_node(&wallpaper_node, "/wallpapers/default.wp", 0);
  CHECK(err, "Failed to open wallpaper file", err);

  uint32_t n = fs_read(&wallpaper_node, 0, wallpaper_node.length, (uint8_t *)wallpaper);
  CHECK(n != backbuf_size, "Failed to read wallpaper file", n);

  ui_redraw_all();

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

uint32_t ui_dispatch_keyboard_event(uint8_t code)
{
  uint32_t eflags = interrupt_save_disable();

  if (responders->size == 0) {
    interrupt_restore(eflags);
    return 0;
  }

  static const uint8_t scancode_meta_pressed = 0x38;
  static const uint8_t scancode_meta_released = 0xb8;
  static const uint8_t scancode_tab_pressed = 0x0f;
  static uint8_t meta_pressed = 0;

  if (meta_pressed && code == scancode_tab_pressed) {
    // Rotate responders list
    ui_responder_t *key_responder = responders->head->value;
    list_remove(responders, responders->head, 0);
    key_responder->list_node->prev = responders->tail;
    key_responder->list_node->next = NULL;
    if (responders->tail)
      responders->tail->next = key_responder->list_node;
    responders->tail = key_responder->list_node;
    if (responders->head == NULL)
      responders->head = key_responder->list_node;
    responders->size++;

    if (responders->size > 1) {
      uint32_t err = ui_dispatch_window_event(key_responder, UI_EVENT_SLEEP);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch sleep event.", err);
      err = ui_dispatch_window_event(responders->head->value, UI_EVENT_WAKE);
      CHECK_RESTORE_EFLAGS(err, "Failed to dispatch wake event.", err);
    }

    ui_redraw_all();

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

  ui_responder_t *key_responder = responders->head->value;
  uint32_t written = fs_write(&key_responder->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);

  interrupt_restore(eflags);
  if (written != sizeof(ui_event_t)) return -1;
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

  klock(&responders_lock);
  if (responders->size == 0) {
    r->window.x = SCREENWIDTH >> 2;
    r->window.y = SCREENHEIGHT >> 2;
  } else {
    r->window.x = BORDER_WIDTH;
    r->window.y = BORDER_WIDTH;
  }

  list_push_front(responders, r);
  r->list_node = responders->head;
  responders_by_gid[p->gid] = r;
  p->has_ui = 1;
  err = ui_dispatch_window_event(r, UI_EVENT_WAKE);
  CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  if (responders->head->next) {
    err = ui_dispatch_window_event(responders->head->next->value, UI_EVENT_SLEEP);
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
  uint8_t is_head = responders->head->value == r;
  list_remove(responders, r->list_node, 1);

  if (is_head && responders->size) {
    uint32_t err = ui_dispatch_window_event(responders->head->value, UI_EVENT_WAKE);
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

  if (r == responders->head->value) {
    ui_blit_window(r, 1);
    for (uint32_t y = 0; y < r->window.h + 2 * BORDER_WIDTH; ++y) {
      uint32_t offset = (y + r->window.y - BORDER_WIDTH) * SCREENWIDTH + r->window.x - BORDER_WIDTH;
      u_memcpy((uint32_t *)buf_vaddr + offset, backbuf + offset,
               (r->window.w + 2 * BORDER_WIDTH) * sizeof(uint32_t));
    }
    interrupt_restore(eflags);
    return 0;
  }

  ui_redraw_all();

  return 0;
}

uint32_t ui_yield(process_t *p)
{
  klock(&responders_lock);
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { kunlock(&responders_lock); return 1; }

  if (responders->head->value != r) { kunlock(&responders_lock); return 0; }

  // Rotate responders list
  list_remove(responders, responders->head, 0);
  r->list_node->prev = responders->tail;
  r->list_node->next = NULL;
  if (responders->tail) responders->tail->next = r->list_node;
  responders->tail = r->list_node;
  if (responders->head == NULL) responders->head = r->list_node;
  responders->size++;

  if (responders->size > 1) {
    ui_responder_t *new_resp = responders->head->value;
    uint32_t err = ui_dispatch_window_event(r, UI_EVENT_SLEEP);
    CHECK_UNLOCK_R(err, "Failed to dispatch sleep event.", err);
    err = ui_dispatch_window_event(responders->head->value, UI_EVENT_WAKE);
    CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  }

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
