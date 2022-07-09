
// ui.c
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "../libc/stdlib.h"
#include "../libc/mako.h"
#include "../libc/errno.h"
#include "../libc/_syscall.h"
#include "ui.h"

int32_t ui_acquire_window()
{
  // ((SCREENWIDTH / 2) * (SCREENHEIGHT / 2) * 4) / 0x1000
  uint32_t buf = pagealloc(((SCREENWIDTH >> 1) * (SCREENHEIGHT >> 1)) >> 10);
  if (buf == 0) return -1;

  int32_t res = _syscall1(SYSCALL_UI_MAKE_RESPONDER, buf);
  if (res < 0) { errno = -res; return -1; }
  return buf;
}

int32_t ui_swap_buffers()
{
  int32_t res = _syscall0(SYSCALL_UI_SWAP_BUFFERS);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_next_event(ui_event_t *buf)
{
  int32_t res = _syscall1(SYSCALL_UI_NEXT_EVENT, (uint32_t)buf);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t ui_yield()
{
  int32_t res = _syscall0(SYSCALL_UI_YIELD);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

uint32_t ui_poll_events()
{ return _syscall0(SYSCALL_UI_POLL_EVENTS); }
