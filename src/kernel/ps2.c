
// ps2.c
//
// PS2 Keyboard/Mouse driver for Mako.
//
// Author: Ajay Tatachar <ajay.tatachar@gmail.com>

#include "ps2.h"
#include "io.h"
#include "interrupt.h"
#include "ui.h"
#include "log.h"

static const uint8_t PS2_DATA_PORT = 0x60;
static const uint8_t PS2_STATUS_PORT = 0x64;
static const uint8_t PS2_COMMAND_PORT = 0x64;

// PS2 commands
static const uint8_t ENABLE_PORT1 = 0xae;
static const uint8_t ENABLE_PORT2 = 0xa8;
static const uint8_t DISABLE_PORT1 = 0xad;
static const uint8_t DISABLE_PORT2 = 0xa7;
static const uint8_t READ_CONFIG = 0x20;
static const uint8_t WRITE_CONFIG = 0x60;
static const uint8_t MOUSE_WRITE = 0xd4;

// Mouse commands
static const uint8_t MOUSE_SET_DEFAULTS = 0xf6;
static const uint8_t MOUSE_DATA_ON = 0xf4;
static const uint8_t MOUSE_SAMPLE_RATE = 0xf3;
static const uint8_t MOUSE_RESOLUTION = 0xe8;

typedef union {
  uint8_t bits;
  struct {
    uint8_t button_left : 1;
    uint8_t button_right : 1;
    uint8_t button_middle : 1;
    uint8_t valid : 1;
    uint8_t x_sign : 1;
    uint8_t y_sign : 1;
    uint8_t x_ovfl : 1;
    uint8_t y_ovfl : 1;
  };
} mouse_state_t;

static void keyboard_interrupt_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  uint8_t code = inb(PS2_DATA_PORT);
  ui_handle_keyboard_event(code);
}

static void mouse_interrupt_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  static uint8_t mouse_bytes[3];
  static unsigned mouse_byte_idx = 0;

  uint8_t mouse_data = inb(PS2_DATA_PORT);
  if (mouse_byte_idx == 0) {
    mouse_state_t state;
    state.bits = mouse_data;
    if (!state.valid) return;
  }

  mouse_bytes[mouse_byte_idx] = mouse_data;
  ++mouse_byte_idx;

  if (mouse_byte_idx == 3) {
    mouse_byte_idx = 0;

    mouse_state_t state;
    state.bits = mouse_bytes[0];
    int32_t dx = mouse_bytes[1];
    int32_t dy = mouse_bytes[2];
    if (state.x_sign && dx) dx = dx - 0x100;
    if (state.y_sign && dy) dy = dy - 0x100;
    if (state.x_ovfl || state.y_ovfl) {
      dx = 0;
      dy = 0;
    }

    ui_handle_mouse_event(dx, dy, state.button_left, state.button_right);
  }
}

// Wait until the PS2 input buffer is empty in order to write to it.
static void ps2_wait_write()
{ while (inb(PS2_STATUS_PORT) & 0b10); }

// Wait until the PS2 output buffer is full in order to read from it.
static void ps2_wait_read()
{ while (!(inb(PS2_STATUS_PORT) & 1)); }

static void ps2_command(uint8_t command)
{
  ps2_wait_write();
  outb(PS2_COMMAND_PORT, command);
}

static uint8_t ps2_command_reply(uint8_t command)
{
  ps2_wait_write();
  outb(PS2_COMMAND_PORT, command);
  ps2_wait_read();
  return inb(PS2_DATA_PORT);
}

static void ps2_command_arg(uint8_t command, uint8_t arg)
{
  ps2_wait_write();
  outb(PS2_COMMAND_PORT, command);
  ps2_wait_write();
  outb(PS2_DATA_PORT, arg);
}

static uint8_t mouse_write(uint8_t data)
{
  ps2_command_arg(MOUSE_WRITE, data);
  ps2_wait_read();
  return inb(PS2_DATA_PORT);
}

uint32_t ps2_init()
{
  ps2_command(DISABLE_PORT1);
  ps2_command(DISABLE_PORT2);

  // Empty the PS2 output buffer. "Output" and "Input" are
  // from the perspective of the PS2 device, i.e. the CPU reads
  // from the PS2 output buffer and writes to the PS2 input
  // buffer.
  int max_buf_count = 1024;
  while ((inb(PS2_STATUS_PORT) & 1) && max_buf_count > 0) {
    --max_buf_count;
    inb(PS2_DATA_PORT);
  }

  if (max_buf_count == 0) {
    log_error("ps2", "Failed to empty PS2 output buffer\n");
    return 1;
  }

  uint8_t config = ps2_command_reply(READ_CONFIG);
  config |= 0b11; // Enable port 1 and port 2 IRQs
  ps2_command_arg(WRITE_CONFIG, config);

  ps2_command(ENABLE_PORT1);
  ps2_command(ENABLE_PORT2);

  mouse_write(MOUSE_SET_DEFAULTS);
  mouse_write(MOUSE_SAMPLE_RATE);
  mouse_write(10);
  mouse_write(MOUSE_RESOLUTION);
  mouse_write(0);
  mouse_write(MOUSE_DATA_ON);

  register_interrupt_handler(33, keyboard_interrupt_handler);
  register_interrupt_handler(44, mouse_interrupt_handler);
  return 0;
}
