
// keyboard.c
//
// Keyboard driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <drivers/io/io.h>
#include <drivers/framebuffer/framebuffer.h> // for debugging
#include <interrupt/interrupt.h>
#include "keyboard.h"
#include "scancodes.h"

// Constants.
const uint32_t KEYBOARD_INTERRUPT_INDEX = 0x21;
static const uint8_t KEYBOARD_DATA_PORT = 0x60;
#define KEYBOARD_BUFFER_SIZE 512

// The keyboard driver is implemented as a circular buffer.
struct keyboard_buffer_s {
  uint8_t buffer[KEYBOARD_BUFFER_SIZE];
  uint8_t *head;
  uint8_t *tail;
  uint32_t count;
};
typedef struct keyboard_buffer_s keyboard_buffer_t;

// State.
static uint8_t lshift    = 0;
static uint8_t rshift    = 0;
static uint8_t caps_lock = 0;
static keyboard_buffer_t keyboard_buffer;

// Read a scan code from the keyboard data port.
static uint8_t read_scan_code()
{
  return inb(KEYBOARD_DATA_PORT);
}

static char shift_char(char c)
{
  if (c >= 'a' && c <= 'z')
    return c + 'A' - 'a';

  // Numbers
  switch (c) {
  case '0':
    return ')';
  case '1':
    return '!';
  case '2':
    return '@';
  case '3':
    return '#';
  case '4':
    return '$';
  case '5':
    return '%';
  case '6':
    return '^';
  case '7':
    return '&';
  case '8':
    return '*';
  case '9':
    return '(';
  }

  // Special characters.
  switch (c) {
  case '-':
    return '_';
  case '=':
    return '+';
  case '[':
    return '{';
  case ']':
    return '}';
  case '\\':
    return '|';
  case ';':
    return ':';
  case '\'':
    return '\"';
  case ',':
    return '<';
  case '.':
    return '>';
  case '/':
    return '?';
  case '`':
    return '~';
  }

  return c;
}

// Convert a scan code to an ASCII character.
static char scan_code_to_ascii(uint8_t code)
{
  if (code & 0x80) { // 'Key break' i.e a key was released.
    code &= 0x7F; // Clear the bit set by key break.
    switch (code) {
    case KB_SC_LSHIFT:   lshift = 0; break;
    case KB_SC_RSHIFT:   rshift = 0; break;
    case KB_SC_CAPSLOCK: caps_lock = !caps_lock; break;
    }
    return -1;
  }

  char c = -1;
  switch (code) {
  case KB_SC_A:        c = 'a'; break;
  case KB_SC_B:        c = 'b'; break;
  case KB_SC_C:        c = 'c'; break;
  case KB_SC_D:        c = 'd'; break;
  case KB_SC_E:        c = 'e'; break;
  case KB_SC_F:        c = 'f'; break;
  case KB_SC_G:        c = 'g'; break;
  case KB_SC_H:        c = 'h'; break;
  case KB_SC_I:        c = 'i'; break;
  case KB_SC_J:        c = 'j'; break;
  case KB_SC_K:        c = 'k'; break;
  case KB_SC_L:        c = 'l'; break;
  case KB_SC_M:        c = 'm'; break;
  case KB_SC_N:        c = 'n'; break;
  case KB_SC_O:        c = 'o'; break;
  case KB_SC_P:        c = 'p'; break;
  case KB_SC_Q:        c = 'q'; break;
  case KB_SC_R:        c = 'r'; break;
  case KB_SC_S:        c = 's'; break;
  case KB_SC_T:        c = 't'; break;
  case KB_SC_U:        c = 'u'; break;
  case KB_SC_V:        c = 'v'; break;
  case KB_SC_W:        c = 'w'; break;
  case KB_SC_X:        c = 'x'; break;
  case KB_SC_Y:        c = 'y'; break;
  case KB_SC_Z:        c = 'z'; break;
  case KB_SC_0:        c = '0'; break;
  case KB_SC_1:        c = '1'; break;
  case KB_SC_2:        c = '2'; break;
  case KB_SC_3:        c = '3'; break;
  case KB_SC_4:        c = '4'; break;
  case KB_SC_5:        c = '5'; break;
  case KB_SC_6:        c = '6'; break;
  case KB_SC_7:        c = '7'; break;
  case KB_SC_8:        c = '8'; break;
  case KB_SC_9:        c = '9'; break;
  case KB_SC_ENTER:    c = '\n'; break;
  case KB_SC_SPACE:    c = ' '; break;
  case KB_SC_BS:       c = 8; break;
  case KB_SC_DASH:     c = '-'; break;
  case KB_SC_EQUALS:   c = '='; break;
  case KB_SC_LBRACKET: c = '['; break;
  case KB_SC_RBRACKET: c = ']'; break;
  case KB_SC_BSLASH:   c = '\\'; break;
  case KB_SC_SCOLON:   c = ';'; break;
  case KB_SC_QUOTE:    c = '\''; break;
  case KB_SC_COMMA:    c = ','; break;
  case KB_SC_DOT:      c = '.'; break;
  case KB_SC_FSLASH:   c = '/'; break;
  case KB_SC_TILDE:    c = '`'; break;
  case KB_SC_TAB:      c = '\t'; break;
  case KB_SC_LSHIFT:   lshift = 1; break;
  case KB_SC_RSHIFT:   rshift = 1; break;
  default: return -1;
  }

  if (caps_lock && c >= 'a' && c <= 'z')
    c = c + 'A' - 'a';

  if (lshift || rshift)
    c = shift_char(c);

  return c;
}

// Handle keyboard interrupt.
static void keyboard_handle_interrupt(
  cpu_state_t c_state, idt_info_t info, stack_state_t s_state
  )
{
  // Only change keyboard_buffer.tail in this function,
  // changing .head or .buffer introduces race condition with
  // `read'.

  if (keyboard_buffer.count >= KEYBOARD_BUFFER_SIZE)
    return;

  uint8_t code = read_scan_code();
  *(keyboard_buffer.tail) = code;
  ++keyboard_buffer.tail;
  ++keyboard_buffer.count;
  if (keyboard_buffer.tail == keyboard_buffer.buffer + KEYBOARD_BUFFER_SIZE)
    keyboard_buffer.tail = keyboard_buffer.buffer;

  // for debugging
  // char c = scan_code_to_ascii(code);
  // if (c != -1) fb_write(&c, 1);
}

// Initialize the keyboard.
void keyboard_init()
{
  keyboard_buffer.count = 0;
  keyboard_buffer.head = keyboard_buffer.buffer;
  keyboard_buffer.tail = keyboard_buffer.buffer;
  for (uint32_t i = 0; i < KEYBOARD_BUFFER_SIZE; ++i)
    keyboard_buffer.buffer[i] = 0;

  register_interrupt_handler(
    KEYBOARD_INTERRUPT_INDEX, keyboard_handle_interrupt
    );
}
