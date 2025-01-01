
// ui.h
//
// User interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UI_COMMON_H_
#define _UI_COMMON_H_

#include <stdint.h>

#define SCREENWIDTH 1024
#define SCREENHEIGHT 768

typedef enum
{
  UI_EVENT_KEYBOARD,
  UI_EVENT_MOUSE_SCROLL,
  UI_EVENT_MOUSE_CLICK,
  UI_EVENT_MOUSE_UNCLICK,
  UI_EVENT_MOUSE_MOVE,
  UI_EVENT_RESIZE_REQUEST,
  UI_EVENT_WAKE,
  UI_EVENT_SLEEP
} ui_event_type_t;

typedef struct ui_event_s
{
  int8_t vscroll;
  int8_t hscroll;
  uint8_t code;
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  int32_t dx;
  int32_t dy;
  ui_event_type_t type;
} ui_event_t;

#endif /* _UI_COMMON_H_ */
