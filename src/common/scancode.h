
// scancode.h
//
// Keyboard scan codes.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SCANCODES_H_
#define _SCANCODES_H_

#include <stdint.h>

#define KB_SC_A 0x1e
#define KB_SC_B 0x30
#define KB_SC_C 0x2e
#define KB_SC_D 0x20
#define KB_SC_E 0x12
#define KB_SC_F 0x21
#define KB_SC_G 0x22
#define KB_SC_H 0x23
#define KB_SC_I 0x17
#define KB_SC_J 0x24
#define KB_SC_K 0x25
#define KB_SC_L 0x26
#define KB_SC_M 0x32
#define KB_SC_N 0x31
#define KB_SC_O 0x18
#define KB_SC_P 0x19
#define KB_SC_Q 0x10
#define KB_SC_R 0x13
#define KB_SC_S 0x1f
#define KB_SC_T 0x14
#define KB_SC_U 0x16
#define KB_SC_V 0x2f
#define KB_SC_W 0x11
#define KB_SC_X 0x2d
#define KB_SC_Y 0x15
#define KB_SC_Z 0x2c

#define KB_SC_1 0x02
#define KB_SC_2 0x03
#define KB_SC_3 0x04
#define KB_SC_4 0x05
#define KB_SC_5 0x06
#define KB_SC_6 0x07
#define KB_SC_7 0x08
#define KB_SC_8 0x09
#define KB_SC_9 0x0a
#define KB_SC_0 0x0b

#define KB_SC_ENTER 0x1c
#define KB_SC_SPACE 0x39
#define KB_SC_BS 0x0e
#define KB_SC_LSHIFT 0x2a
#define KB_SC_RSHIFT 0x36
#define KB_SC_DASH 0x0c
#define KB_SC_EQUALS 0x0d
#define KB_SC_LBRACKET 0x1a
#define KB_SC_RBRACKET 0x1b
#define KB_SC_BSLASH 0x2b
#define KB_SC_SCOLON 0x27
#define KB_SC_QUOTE 0x28
#define KB_SC_COMMA 0x33
#define KB_SC_DOT 0x34
#define KB_SC_FSLASH 0x35
#define KB_SC_TILDE 0x29
#define KB_SC_CAPSLOCK 0x3a
#define KB_SC_TAB 0x0f
#define KB_SC_ESC 0x01
#define KB_SC_META 0x38
#define KB_SC_LEFT 0x4B
#define KB_SC_RIGHT 0x4D
#define KB_SC_UP 0x48
#define KB_SC_DOWN 0x50

#define KB_KEY_RELEASED_MASK 0x80

char scancode_to_ascii(uint8_t sc, uint8_t shift)
{
  char c = 0;
  switch (sc) {
    case KB_SC_A:
      c = 'a';
      break;
    case KB_SC_B:
      c = 'b';
      break;
    case KB_SC_C:
      c = 'c';
      break;
    case KB_SC_D:
      c = 'd';
      break;
    case KB_SC_E:
      c = 'e';
      break;
    case KB_SC_F:
      c = 'f';
      break;
    case KB_SC_G:
      c = 'g';
      break;
    case KB_SC_H:
      c = 'h';
      break;
    case KB_SC_I:
      c = 'i';
      break;
    case KB_SC_J:
      c = 'j';
      break;
    case KB_SC_K:
      c = 'k';
      break;
    case KB_SC_L:
      c = 'l';
      break;
    case KB_SC_M:
      c = 'm';
      break;
    case KB_SC_N:
      c = 'n';
      break;
    case KB_SC_O:
      c = 'o';
      break;
    case KB_SC_P:
      c = 'p';
      break;
    case KB_SC_Q:
      c = 'q';
      break;
    case KB_SC_R:
      c = 'r';
      break;
    case KB_SC_S:
      c = 's';
      break;
    case KB_SC_T:
      c = 't';
      break;
    case KB_SC_U:
      c = 'u';
      break;
    case KB_SC_V:
      c = 'v';
      break;
    case KB_SC_W:
      c = 'w';
      break;
    case KB_SC_X:
      c = 'x';
      break;
    case KB_SC_Y:
      c = 'y';
      break;
    case KB_SC_Z:
      c = 'z';
      break;
    case KB_SC_0:
      c = '0';
      break;
    case KB_SC_1:
      c = '1';
      break;
    case KB_SC_2:
      c = '2';
      break;
    case KB_SC_3:
      c = '3';
      break;
    case KB_SC_4:
      c = '4';
      break;
    case KB_SC_5:
      c = '5';
      break;
    case KB_SC_6:
      c = '6';
      break;
    case KB_SC_7:
      c = '7';
      break;
    case KB_SC_8:
      c = '8';
      break;
    case KB_SC_9:
      c = '9';
      break;
    case KB_SC_ENTER:
      c = '\n';
      break;
    case KB_SC_SPACE:
      c = ' ';
      break;
    case KB_SC_BS:
      c = 8;
      break;
    case KB_SC_DASH:
      c = '-';
      break;
    case KB_SC_EQUALS:
      c = '=';
      break;
    case KB_SC_LBRACKET:
      c = '[';
      break;
    case KB_SC_RBRACKET:
      c = ']';
      break;
    case KB_SC_BSLASH:
      c = '\\';
      break;
    case KB_SC_SCOLON:
      c = ';';
      break;
    case KB_SC_QUOTE:
      c = '\'';
      break;
    case KB_SC_COMMA:
      c = ',';
      break;
    case KB_SC_DOT:
      c = '.';
      break;
    case KB_SC_FSLASH:
      c = '/';
      break;
    case KB_SC_TILDE:
      c = '`';
      break;
    case KB_SC_TAB:
      c = '\t';
      break;
  }

  if (shift) {
    if (c >= 'a' && c <= 'z')
      return c + 'A' - 'a';

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
  }

  return c;
}

#endif /* _SCANCODES_H_ */
