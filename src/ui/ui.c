
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <process/process.h>
#include <kheap/kheap.h>
#include <klock/klock.h>
#include <ds/ds.h>
#include <util/util.h>
#include <paging/paging.h>
#include <interrupt/interrupt.h>
#include <debug/log.h>
#include "ui.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("ui", msg "\n"); return (code);   \
  }
#define CHECK_UNLOCK_R(err, msg, code) if ((err)) {         \
    log_error("ui", msg "\n"); kunlock(&responders_lock);   \
    return (code);                                          \
  }
#define CHECK_UNLOCK_F(err, msg, code) if ((err)) {         \
    log_error("ui", msg "\n"); kunlock(&free_windows_lock); \
    return (code);                                          \
  }

static uint32_t buf_vaddr = 0;
static ui_responder_t *key_responder = NULL;
static list_t *responders = NULL;
static list_t *free_windows = NULL;
static volatile uint32_t responders_lock = 0;
static volatile uint32_t free_windows_lock = 0;

uint32_t ui_init(uint32_t vaddr)
{
  buf_vaddr = vaddr;

  responders = kmalloc(sizeof(list_t));
  CHECK(responders == NULL, "No memory.", ENOMEM);
  u_memset(responders, 0, sizeof(list_t));

  free_windows = kmalloc(sizeof(list_t));
  CHECK(free_windows == NULL, "No memory.", ENOMEM);
  u_memset(free_windows, 0, sizeof(list_t));

  ui_window_t *w = kmalloc(sizeof(ui_window_t));
  CHECK(w == NULL, "No memory.", ENOMEM);
  w->x = 0;
  w->y = 0;
  w->w = SCREENWIDTH;
  w->h = SCREENHEIGHT;
  list_push_back(free_windows, w);

  return 0;
}

static uint32_t ui_dispatch_resize_event(ui_responder_t *r)
{
  ui_event_t *ev = kmalloc(sizeof(ui_event_t));
  CHECK(ev == NULL, "No memory.", ENOMEM);
  ev->type = UI_EVENT_RESIZE;
  ev->width = r->window.w;
  ev->height = r->window.h;

  klock(&(r->lock));
  list_push_back(r->process->ui_event_queue, ev);
  kunlock(&(r->lock));
  r->process->is_running = 1;
  return 0;
}

uint32_t ui_dispatch_keyboard_event(uint8_t code)
{
  if (key_responder == NULL) return 0;

  ui_event_t *ev = kmalloc(sizeof(ui_event_t));
  CHECK(ev == NULL, "No memory.", ENOMEM);
  ev->type = UI_EVENT_KEYBOARD;
  ev->code = code;

  klock(&(key_responder->lock));
  list_push_back(key_responder->process->ui_event_queue, ev);
  kunlock(&(key_responder->lock));
  key_responder->process->is_running = 1;

  return 0;
}

static ui_responder_t *responder_from_process(process_t *p)
{
  ui_responder_t *r = NULL;
  klock(&responders_lock);
  if (key_responder && key_responder->process->pid == p->pid) {
    kunlock(&responders_lock);
    return key_responder;
  }
  list_foreach(lnode, responders) {
    ui_responder_t *lr = lnode->value;
    if (lr->process == p) {
      r = lr; break;
    }
  }
  kunlock(&responders_lock);
  return r;
}

uint32_t ui_split(process_t *p, ui_split_type_t type)
{
  ui_responder_t *r = responder_from_process(p);
  if (r == NULL) return EINVAL;

  klock(&free_windows_lock);

  ui_window_t *w = kmalloc(sizeof(ui_window_t));
  CHECK_UNLOCK_F(w == NULL, "No memory.", ENOMEM);
  switch (type) {
  case UI_SPLIT_LEFT:
    w->x = r->window.x;
    w->y = r->window.y;
    w->w = r->window.w / 2;
    w->h = r->window.h;
    r->window.w /= 2;
    r->window.x += r->window.w;
    break;
  case UI_SPLIT_RIGHT:
    w->x = r->window.x + (r->window.w / 2);
    w->y = r->window.y;
    w->w = r->window.w / 2;
    w->h = r->window.h;
    r->window.w /= 2;
    break;
  case UI_SPLIT_UP:
    w->x = r->window.x;
    w->y = r->window.y;
    w->w = r->window.w;
    w->h = r->window.h / 2;
    r->window.h /= 2;
    r->window.y += r->window.h;
    break;
  case UI_SPLIT_DOWN:
    w->x = r->window.x;
    w->y = r->window.y + (r->window.h / 2);
    w->w = r->window.w;
    w->h = r->window.h / 2;
    r->window.h /= 2;
    break;
  }
  ui_dispatch_resize_event(r);
  list_push_back(free_windows, w);

  kunlock(&free_windows_lock);
  return 0;
}

uint32_t ui_make_responder(process_t *p)
{
  klock(&free_windows_lock);
  if (free_windows->size == 0) return ENOSPC;

  klock(&responders_lock);
  ui_responder_t *r = kmalloc(sizeof(ui_responder_t));
  if (r == NULL) {
    kunlock(&responders_lock);
    kunlock(&free_windows_lock);
    return ENOMEM;
  }
  r->process = p;
  r->window = *((ui_window_t *)free_windows->head->value);
  list_pop_front(free_windows);
  kunlock(&free_windows_lock);
  r->lock = 0;
  list_push_back(responders, r);
  r->list_node = responders->tail;

  ui_event_t *ev = kmalloc(sizeof(ui_event_t));
  ev->type = UI_EVENT_WAKE;
  ev->width = r->window.w;
  ev->height = r->window.h;
  CHECK(ev == NULL, "No memory.", ENOMEM);
  klock(&(r->lock));
  list_push_back(r->process->ui_event_queue, ev);
  kunlock(&(r->lock));
  r->process->is_running = 1;

  if (key_responder == NULL) key_responder = r;

  kunlock(&responders_lock);
  return 0;
}

uint32_t ui_kill(process_t *p)
{
  return 0;
}

uint32_t ui_swap_buffers(process_t *p, uint32_t backbuf_vaddr)
{
  ui_responder_t *r = responder_from_process(p);
  if (r == NULL) return EINVAL;
  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  uint32_t *backbuf = (uint32_t *)backbuf_vaddr;
  uint32_t *b_row = backbuf;
  uint32_t *buf = (uint32_t *)buf_vaddr;
  uint32_t *row = buf + (SCREENWIDTH * r->window.y);

  if (r->window.w == SCREENWIDTH && r->window.h == SCREENHEIGHT) {
    u_memcpy(buf, backbuf, SCREENWIDTH * SCREENHEIGHT * 4);
    paging_set_cr3(cr3);
    interrupt_restore(eflags);
    return 0;
  }

  for (uint32_t i = r->window.y; i < r->window.y + r->window.h; ++i) {
    u_memcpy(row + r->window.x, b_row, r->window.w * 4);
    row += SCREENWIDTH;
    b_row += r->window.w;
  }

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
  return 0;
}

uint32_t ui_yield(process_t *p)
{
  if (p->pid != key_responder->process->pid) return EINVAL;
  klock(&responders_lock);
  list_node_t *next = key_responder->list_node->next;
  if (next == NULL) next = responders->head;
  key_responder = next->value;
  kunlock(&responders_lock);

  ui_event_t *ev = kmalloc(sizeof(ui_event_t));
  ev->type = UI_EVENT_WAKE;
  ev->width = key_responder->window.w;
  ev->height = key_responder->window.h;
  CHECK(ev == NULL, "No memory.", ENOMEM);
  klock(&(key_responder->lock));
  list_push_back(key_responder->process->ui_event_queue, ev);
  kunlock(&(key_responder->lock));
  key_responder->process->is_running = 1;
  return 0;
}
