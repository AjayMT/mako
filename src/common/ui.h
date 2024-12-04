
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
  UI_EVENT_SCROLL,
  UI_EVENT_RESIZE,
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
  ui_event_type_t type;
} ui_event_t;

#endif /* _UI_COMMON_H_ */
