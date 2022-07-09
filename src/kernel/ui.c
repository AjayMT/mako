
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

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("ui", msg "\n"); return (code);   \
  }
#define CHECK_UNLOCK_R(err, msg, code) if ((err)) {             \
    log_error("ui", msg "\n"); kunlock(&responders_lock);       \
    return (code);                                              \
  }

typedef struct ui_responder_s {
  process_t *process;
  ui_window_t window;
  uint32_t *buf;
  fs_node_t event_pipe_read;
  fs_node_t event_pipe_write;
  list_node_t *list_node;
} ui_responder_t;

static uint32_t buf_vaddr = 0;
static uint32_t *backbuf = NULL;
static volatile uint32_t backbuf_lock;
static list_t *responders = NULL;
static volatile uint32_t responders_lock = 0;

static ui_responder_t *responders_by_gid[MAX_PROCESS_COUNT];

// Blit a single window to the backbuf.
static void ui_blit_window(ui_responder_t *r, uint8_t is_key)
{
  (void)is_key;
  process_t *p = r->process;
  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);
  // TODO window borders
  for (uint32_t y = 0; y < r->window.h; ++y) {
    uint32_t *r_buf_ptr = r->buf + (y * r->window.w);
    uint32_t *backbuf_ptr = backbuf + ((y + r->window.y) * SCREENWIDTH) + r->window.x;
    u_memcpy(backbuf_ptr, r_buf_ptr, r->window.w * 4);
  }

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
}

// Redraw the entire screen.
static void ui_redraw_all()
{
  klock(&backbuf_lock);

  for (uint32_t i = 0; i < SCREENWIDTH * SCREENHEIGHT; ++i)
    backbuf[i] = 0x777777;

  klock(&responders_lock);
  for (list_node_t *current = responders->tail; current; current = current->prev) {
    ui_responder_t *responder = current->value;
    ui_blit_window(responder, current == responders->head);
  }
  kunlock(&responders_lock);

  uint32_t eflags = interrupt_save_disable();
  u_memcpy((uint32_t *)buf_vaddr, backbuf, SCREENWIDTH * SCREENHEIGHT * 4);
  interrupt_restore(eflags);

  kunlock(&backbuf_lock);
}

uint32_t ui_init(uint32_t vaddr)
{
  u_memset(responders_by_gid, 0, sizeof(responders_by_gid));
  buf_vaddr = vaddr;
  backbuf = kmalloc(SCREENHEIGHT * SCREENWIDTH * 4); CHECK(backbuf == NULL, "No memory.", ENOMEM);
  responders = kmalloc(sizeof(list_t)); CHECK(responders == NULL, "No memory.", ENOMEM);
  u_memset(responders, 0, sizeof(list_t));

  ui_redraw_all();

  return 0;
}

static uint32_t ui_dispatch_window_event(ui_responder_t *r, ui_event_type_t t)
{
  ui_event_t ev;
  ev.type = t;
  ev.width = r->window.w;
  ev.height = r->window.h;

  uint32_t eflags = interrupt_save_disable();
  uint32_t written = fs_write(&r->event_pipe_write, 0, sizeof(ui_event_t), (uint8_t *)&ev);
  interrupt_restore(eflags);

  if (written != sizeof(ui_event_t)) return -1;
  return 0;
}

uint32_t ui_dispatch_keyboard_event(uint8_t code)
{
  ui_event_t ev;
  ev.type = UI_EVENT_KEYBOARD;
  ev.code = code;

  uint32_t eflags = interrupt_save_disable();
  if (responders->size == 0) { interrupt_restore(eflags); return 0; }

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
    ui_responder_t *key_responder = responders->head->value;
    r->window.x = key_responder->window.x + 16;
    r->window.y = key_responder->window.y + 16;
    if (r->window.x >= SCREENWIDTH) r->window.x = 0;
    if (r->window.x >= SCREENHEIGHT) r->window.x = 0;
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
  if (is_head) {
    uint32_t err = ui_dispatch_window_event(responders->head->value, UI_EVENT_WAKE);
    CHECK_UNLOCK_R(err, "Failed to dispatch wake event.", err);
  }
  kunlock(&responders_lock);
  ui_redraw_all();
  return 0;
}

uint32_t ui_swap_buffers(process_t *p)
{
  // acquire backbuf lock first to avoid deadlock with ui_redraw_all
  klock(&backbuf_lock);
  klock(&responders_lock);

  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { kunlock(&responders_lock); klock(&backbuf_lock); return 1; }

  if (r == responders->head->value) {
    ui_blit_window(r, 1);

    uint32_t eflags = interrupt_save_disable();
    for (uint32_t y = 0; y < r->window.w; ++y) {
      uint32_t offset = (y + r->window.y) * SCREENWIDTH + r->window.x;
      u_memcpy((uint32_t *)buf_vaddr + offset, backbuf + offset, r->window.w * 4);
    }
    interrupt_restore(eflags);

    kunlock(&responders_lock);
    kunlock(&backbuf_lock);
    return 0;
  }

  kunlock(&responders_lock);
  kunlock(&backbuf_lock);

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
  uint32_t eflags = interrupt_save_disable();
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) { interrupt_restore(eflags); return 1; }

  uint8_t ev_buf[sizeof(ui_event_t)];
  uint32_t read_size = fs_read(&r->event_pipe_read, 0, sizeof(ui_event_t), ev_buf);
  if (read_size < sizeof(ui_event_t)) {
    interrupt_restore(eflags);
    return 1;
  }

  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);
  u_memcpy((uint8_t *)buf, ev_buf, sizeof(ui_event_t));
  paging_set_cr3(cr3);

  interrupt_restore(eflags);

  return 0;
}

uint32_t ui_poll_events(process_t *p)
{
  uint32_t eflags = interrupt_save_disable();
  ui_responder_t *r = responders_by_gid[p->gid];
  if (r == NULL) return 0;
  uint32_t count = r->event_pipe_read.length / sizeof(ui_event_t);
  interrupt_restore(eflags);
  return count;
}
