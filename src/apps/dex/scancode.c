
// scancode.c
//
// Keyboard scan codes.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include "scancode.h"

static char shift_char(char c)
{
  if (c >= 'a' && c <= 'z')
    return c + 'A' - 'a';

  switch (c) {
  case '0': return ')';
  case '1': return '!';
  case '2': return '@';
  case '3': return '#';
  case '4': return '$';
  case '5': return '%';
  case '6': return '^';
  case '7': return '&';
  case '8': return '*';
  case '9': return '(';
  case '-': return '_';
  case '=': return '+';
  case '[': return '{';
  case ']': return '}';
  case '\\': return '|';
  case ';': return ':';
  case '\'': return '\"';
  case ',': return '<';
  case '.': return '>';
  case '/': return '?';
  case '`': return '~';
  }

  return c;
}

char scancode_to_ascii(uint8_t sc, uint8_t shift)
{
  char c = 0;
  switch (sc) {
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
  }

  if (shift) c = shift_char(c);

  return c;
}
