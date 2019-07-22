
// printf.c
//
// Printf functions.
//
// Taken from ToAruOS <http://github.com/klange/toaruos>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static void print_dec(
  uint32_t value,
  uint32_t width,
  char *buf,
  int32_t *ptr,
  int32_t fill_zero,
  int32_t align_right
  ) {
  uint32_t n_width = 1;
  uint32_t i = 9;

  if (value < 10UL) {
    n_width = 1;
  } else if (value < 100UL) {
    n_width = 2;
  } else if (value < 1000UL) {
    n_width = 3;
  } else if (value < 10000UL) {
    n_width = 4;
  } else if (value < 100000UL) {
    n_width = 5;
  } else if (value < 1000000UL) {
    n_width = 6;
  } else if (value < 10000000UL) {
    n_width = 7;
  } else if (value < 100000000UL) {
    n_width = 8;
  } else if (value < 1000000000UL) {
    n_width = 9;
  } else {
    n_width = 10;
  }

  int32_t printed = 0;
  if (align_right) {
    while (n_width + printed < width) {
      buf[*ptr] = fill_zero ? '0' : ' ';
      *ptr += 1;
      printed += 1;
    }

    i = n_width;
    while (i > 0) {
      uint32_t n = value / 10;
      int32_t r = value % 10;
      buf[*ptr + i - 1] = r + '0';
      i--;
      value = n;
    }
    *ptr += n_width;
  } else {
    i = n_width;
    while (i > 0) {
      uint32_t n = value / 10;
      int32_t r = value % 10;
      buf[*ptr + i - 1] = r + '0';
      i--;
      value = n;
      printed++;
    }
    *ptr += n_width;
    while (printed < (int)width) {
      buf[*ptr] = fill_zero ? '0' : ' ';
      *ptr += 1;
      printed += 1;
    }
  }
}

/*
 * Hexadecimal to string
 */
static void print_hex(
  uint32_t value, uint32_t width, char * buf, int32_t * ptr
  ) {
  int32_t i = width;

  if (i == 0) i = 8;

  uint32_t n_width = 1;
  uint32_t j = 0x0F;
  while (value > j && j < UINT32_MAX) {
    n_width += 1;
    j *= 0x10;
    j += 0x0F;
  }

  while (i > (int)n_width) {
    buf[*ptr] = '0';
    *ptr += 1;
    i--;
  }

  i = (int)n_width;
  while (i-- > 0) {
    buf[*ptr] = "0123456789abcdef"[(value>>(i*4))&0xF];
    *ptr += + 1;
  }
}

/*
 * vasprintf()
 */
int32_t xvasprintf(char * buf, const char * fmt, va_list args) {
  int32_t i = 0;
  char * s;
  char * b = buf;
  int32_t precision = -1;
  for (const char *f = fmt; *f; f++) {
    if (*f != '%') {
      *b++ = *f;
      continue;
    }
    ++f;
    uint32_t arg_width = 0;
    int32_t align = 1; /* right */
    int32_t fill_zero = 0;
    int32_t big = 0;
    int32_t alt = 0;
    int32_t always_sign = 0;
    while (1) {
      if (*f == '-') {
        align = 0;
        ++f;
      } else if (*f == '#') {
        alt = 1;
        ++f;
      } else if (*f == '*') {
        arg_width = (char)va_arg(args, int);
        ++f;
      } else if (*f == '0') {
        fill_zero = 1;
        ++f;
      } else if (*f == '+') {
        always_sign = 1;
        ++f;
      } else {
        break;
      }
    }
    while (*f >= '0' && *f <= '9') {
      arg_width *= 10;
      arg_width += *f - '0';
      ++f;
    }
    if (*f == '.') {
      ++f;
      precision = 0;
      if (*f == '*') {
        precision = (int)va_arg(args, int);
        ++f;
      } else  {
        while (*f >= '0' && *f <= '9') {
          precision *= 10;
          precision += *f - '0';
          ++f;
        }
      }
    }
    if (*f == 'l') {
      big = 1;
      ++f;
      if (*f == 'l') {
        big = 2;
        ++f;
      }
    }
    if (*f == 'z') {
      big = 1;
      ++f;
    }
    /* fmt[i] == '%' */
    switch (*f) {
    case 's': /* String pointer -> String */
    {
      size_t count = 0;
      if (big) {
        wchar_t * ws = (wchar_t *)va_arg(args, wchar_t *);
        if (ws == NULL) {
          ws = L"(null)";
        }
        size_t count = 0;
        while (*ws) {
          *b++ = *ws++;
          count++;
          if (arg_width && count == arg_width) break;
        }
      } else {
        s = (char *)va_arg(args, char *);
        if (s == NULL) {
          s = "(null)";
        }
        if (precision >= 0) {
          while (*s && precision > 0) {
            *b++ = *s++;
            count++;
            precision--;
            if (arg_width && count == arg_width) break;
          }
        } else {
          while (*s) {
            *b++ = *s++;
            count++;
            if (arg_width && count == arg_width) break;
          }
        }
      }
      while (count < arg_width) {
        *b++ = ' ';
        count++;
      }
    }
    break;
    case 'c': /* Single character */
      *b++ = (char)va_arg(args, int);
      break;
    case 'p':
      if (!arg_width) {
        arg_width = 8;
      }
    case 'x': /* Hexadecimal number */
      if (alt) {
        *b++ = '0';
        *b++ = 'x';
      }
      i = b - buf;
      if (big == 2) {
        unsigned long long val = (unsigned long long)va_arg(args, unsigned long long);
        if (val > 0xFFFFFFFF) {
          print_hex(val >> 32, arg_width > 8 ? (arg_width - 8) : 0, buf, &i);
        }
        print_hex(val & 0xFFFFFFFF, arg_width > 8 ? 8 : arg_width, buf, &i);
      } else {
        print_hex((unsigned long)va_arg(args, unsigned long), arg_width, buf, &i);
      }
      b = buf + i;
      break;
    case 'i':
    case 'd': /* Decimal number */
      i = b - buf;
      {
        long long val;
        if (big == 2) {
          val = (long long)va_arg(args, long long);
        } else {
          val = (long)va_arg(args, long);
        }
        if (val < 0) {
          *b++ = '-';
          buf++;
          val = -val;
        } else if (always_sign) {
          *b++ = '+';
          buf++;
        }
        print_dec(val, arg_width, buf, &i, fill_zero, align);
      }
      b = buf + i;
      break;
    case 'u': /* Unsigned ecimal number */
      i = b - buf;
      {
        unsigned long long val;
        if (big == 2) {
          val = (unsigned long long)va_arg(args, unsigned long long);
        } else {
          val = (unsigned long)va_arg(args, unsigned long);
        }
        print_dec(val, arg_width, buf, &i, fill_zero, align);
      }
      b = buf + i;
      break;
    case 'g': /* supposed to also support e */
    case 'f':
    {
      double val = (double)va_arg(args, double);
      i = b - buf;
      if (val < 0) {
        *b++ = '-';
        buf++;
        val = -val;
      }
      print_dec((long)val, arg_width, buf, &i, fill_zero, align);
      b = buf + i;
      i = b - buf;
      *b++ = '.';
      buf++;
      for (int32_t j = 0; j < ((precision > -1 && precision < 8) ? precision : 8); ++j) {
        if ((int)(val * 100000.0) % 100000 == 0 && j != 0) break;
        val *= 10.0;
        print_dec((int)(val) % 10, 0, buf, &i, 0, 0);
      }
      b = buf + i;
    }
    break;
    case '%': /* Escape */
      *b++ = '%';
      break;
    default: /* Nothing at all, just dump it */
      *b++ = *f;
      break;
    }
  }
  /* Ensure the buffer ends in a null */
  *b = '\0';
  return b - buf;
}

int32_t vasprintf(char ** buf, const char * fmt, va_list args) {
  char * b = malloc(1024);
  *buf = b;
  return xvasprintf(b, fmt, args);
}

int32_t vsprintf(char * buf, const char *fmt, va_list args) {
  return xvasprintf(buf, fmt, args);
}

int32_t vsnprintf(char * buf, size_t size, const char *fmt, va_list args) {
  /* XXX */
  return xvasprintf(buf, fmt, args);
}

int32_t vfprintf(FILE * device, const char *fmt, va_list args) {
  char * buffer;
  vasprintf(&buffer, fmt, args);

  int32_t out = fwrite(buffer, 1, strlen(buffer), device);
  free(buffer);
  return out;
}

int32_t vprintf(const char *fmt, va_list args) {
  return vfprintf(stdout, fmt, args);
}

int32_t fprintf(FILE * device, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char * buffer;
  vasprintf(&buffer, fmt, args);
  va_end(args);

  int32_t out = fwrite(buffer, 1, strlen(buffer), device);
  free(buffer);
  return out;
}

int32_t printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char * buffer;
  vasprintf(&buffer, fmt, args);
  va_end(args);
  int32_t out = fwrite(buffer, 1, strlen(buffer), stdout);
  free(buffer);
  return out;
}

int32_t sprintf(char * buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int32_t out = xvasprintf(buf, fmt, args);
  va_end(args);
  return out;
}

int32_t snprintf(char * buf, size_t size, const char * fmt, ...) {
  /* XXX This is bad. */
  (void)size;
  va_list args;
  va_start(args, fmt);
  int32_t out = xvasprintf(buf, fmt, args);
  va_end(args);
  return out;
}
