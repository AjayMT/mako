
// keyboard.c
//
// Keyboard driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "io.h"
#include "interrupt.h"
#include "../common/errno.h"
#include "fs.h"
#include "ringbuffer.h"
#include "ui.h"
#include "kheap.h"
#include "util.h"
#include "log.h"
#include "keyboard.h"

static ringbuffer_t *rb = NULL;

static void keyboard_interrupt_handler()
{
  uint8_t code = inb(0x60);
  ui_dispatch_keyboard_event(code);
  // ringbuffer_write(rb, 1, &code);
}

static uint32_t kbd_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{ return ringbuffer_read(rb, size, buf); }

uint32_t keyboard_init()
{
  rb = ringbuffer_create(512);
  if (rb == NULL) return ENOMEM;

  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  if (node == NULL) return ENOMEM;
  u_memset(node, 0, sizeof(fs_node_t));
  u_memcpy(node->name, "kbd", 4);
  node->read = kbd_read;
  // uint32_t res = fs_mount(node, "/dev/kbd");
  // if (res) return res;

  register_interrupt_handler(33, keyboard_interrupt_handler);

  return 0;
}
