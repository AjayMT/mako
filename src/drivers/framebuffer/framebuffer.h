
// framebuffer.h
//
// VGA frame driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

// VGA color definitions.
// See <https://en.wikipedia.org/wiki/Video_Graphics_Array#Color_palette>
extern const unsigned short FB_BLACK;
extern const unsigned short FB_BLUE;
extern const unsigned short FB_GREEN;
extern const unsigned short FB_CYAN;
extern const unsigned short FB_RED;
extern const unsigned short FB_MAGENTA;
extern const unsigned short FB_BROWN;
extern const unsigned short FB_LIGHT_GREY;
extern const unsigned short FB_DARK_GREY;
extern const unsigned short FB_LIGHT_BLUE;
extern const unsigned short FB_LIGHT_GREEN;
extern const unsigned short FB_LIGHT_CYAN;
extern const unsigned short FB_LIGHT_RED;
extern const unsigned short FB_LIGHT_MAGENTA;
extern const unsigned short FB_LIGHT_BROWN;
extern const unsigned short FB_WHITE;

// Clear the screen.
void fb_clear();

// Write `data` of length `len` to the frame.
// Automatically scrolls and advances cursor.
void fb_write(const char *data, const unsigned int len);

// Set colors.
void fb_set_fg_color(const unsigned short color);
void fb_set_bg_color(const unsigned short color);

#endif /* _FRAMEBUFFER_H_ */
