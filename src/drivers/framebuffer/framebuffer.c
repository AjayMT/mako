
// framebuffer.c
//
// VGA frame buffer driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <drivers/io/io.h>
#include "framebuffer.h"

// VGA color definitions.
// See <https://en.wikipedia.org/wiki/Video_Graphics_Array#Color_palette>
const unsigned short FB_BLACK         = 0x0;
const unsigned short FB_BLUE          = 0x1;
const unsigned short FB_GREEN         = 0x2;
const unsigned short FB_CYAN          = 0x3;
const unsigned short FB_RED           = 0x4;
const unsigned short FB_MAGENTA       = 0x5;
const unsigned short FB_BROWN         = 0x6;
const unsigned short FB_LIGHT_GREY    = 0x7;
const unsigned short FB_DARK_GREY     = 0x8;
const unsigned short FB_LIGHT_BLUE    = 0x9;
const unsigned short FB_LIGHT_GREEN   = 0xA;
const unsigned short FB_LIGHT_CYAN    = 0xB;
const unsigned short FB_LIGHT_RED     = 0xC;
const unsigned short FB_LIGHT_MAGENTA = 0xD;
const unsigned short FB_LIGHT_BROWN   = 0xE;
const unsigned short FB_WHITE         = 0xF;

// Buffer constants.
static volatile short *const VGA_BUFFER = (short *)0xB8000;
static const unsigned int VGA_HEIGHT    = 25;
static const unsigned int VGA_WIDTH     = 80;

// Serial port constants.
static const unsigned short FB_COMMAND_PORT     = 0x3D4;
static const unsigned short FB_DATA_PORT        = 0x3D5;
static const unsigned char FB_HIGH_BYTE_COMMAND = 14;
static const unsigned char FB_LOW_BYTE_COMMAND  = 15;

// Driver state.
static unsigned short fg_color = FB_WHITE;
static unsigned short bg_color = FB_BLACK;
static unsigned int position   = 0;

// Write a character to current position in current color.
static inline void fb_write_cell(const char data)
{
  VGA_BUFFER[position] = data | ((fg_color | bg_color) << 8);
}

// Move the cursor to the current position.
static void fb_move_cursor()
{
  outb(FB_COMMAND_PORT, FB_HIGH_BYTE_COMMAND);
  outb(FB_DATA_PORT,    ((position >> 8) & 0x00FF));
  outb(FB_COMMAND_PORT, FB_LOW_BYTE_COMMAND);
  outb(FB_DATA_PORT,    position & 0x00FF);
}

// Clear the screen.
void fb_clear()
{
  for (unsigned int pos = 0; pos < VGA_WIDTH * VGA_HEIGHT; ++pos)
    VGA_BUFFER[pos] = ' ' | ((fg_color | bg_color) << 8);

  position = 0;
  fb_move_cursor();
}

// Write `data` of length `len` to the frame.
// Automatically scrolls and advances cursor.
void fb_write(const char *data, const unsigned int len)
{
  for (unsigned int i = 0; i < len; ++i) {
    if (position >= VGA_WIDTH * VGA_HEIGHT) { // Clear the screen.
      fb_clear();
      continue;
    }

    if (data[i] == '\n') { // Move to the next line.
      position += VGA_WIDTH;
      position -= (position % VGA_WIDTH);
      continue;
    }

    fb_write_cell(data[i]);
    ++position;
  }
  fb_move_cursor();
}

// Set colors.
void fb_set_fg_color(const unsigned short color)
{ fg_color = color; }
void fb_set_bg_color(const unsigned short color)
{ bg_color = color; }
