
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "../libc/stdlib.h"
#include <stddef.h>
#include "../libc/errno.h"
#include "../libc/_syscall.h"
#include "ui.h"

static ui_event_handler_t handler = NULL;
static ui_event_t *event_buffer = NULL;

static void ui_event_handle()
{
  if (handler && event_buffer) handler(*event_buffer);
  _syscall0(SYSCALL_UI_RESUME);
}

int32_t ui_init()
{
  event_buffer = malloc(sizeof(ui_event_t));
  if (event_buffer == NULL) { errno = ENOMEM; return -1; }
  _syscall2(
    SYSCALL_UI_REGISTER, (uint32_t)ui_event_handle, (uint32_t)event_buffer
    );
  return 0;
}

void ui_set_handler(ui_event_handler_t h)
{ handler = h; }

int32_t ui_acquire_window()
{
  int32_t res = _syscall0(SYSCALL_UI_MAKE_RESPONDER);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_split(ui_split_type_t type)
{
  int32_t res = _syscall1(SYSCALL_UI_SPLIT, type);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_swap_buffers(uint32_t buf)
{
  int32_t res = _syscall1(SYSCALL_UI_SWAP_BUFFERS, buf);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

void ui_wait()
{ _syscall0(SYSCALL_UI_WAIT); }

int32_t ui_yield()
{
  int32_t res = _syscall0(SYSCALL_UI_YIELD);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
