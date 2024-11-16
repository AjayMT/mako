
// log.c
//
// Logging functions.
//
// Adapted from <http://github.com/littleosbook/aenix>.

#include <stdarg.h>
#include "../common/stdint.h"
#include "serial.h"
#include "log.h"

#define LOG_COM SERIAL_COM1_BASE

static void log_ui(uint32_t i)
{
  uint32_t n = 1;
  if (i >= 1000000000) n = 1000000000;
  else while (n*10 <= i) n *= 10;

  while (n > 0) {
    serial_write(LOG_COM, '0' + (i / n));
    i %= n;
    n /= 10;
  }
}

static void log_i(int32_t i)
{
  if (i < 0) {
    serial_write(LOG_COM, '-');
    i = -i;
  }

  int32_t n = 1;
  if (i >= 1000000000) n = 1000000000;
  else while (n*10 <= i) n *= 10;

  while (n > 0) {
    serial_write(LOG_COM, '0' + (i / n));
    i %= n;
    n /= 10;
  }
}

static void log_hex(uint32_t i)
{
  char *digits = "0123456789ABCDEF";
  uint32_t n = 0, min_digits = 8;

  if (i >= 0x10000000) n = 28;
  else while ((uint32_t)(1 << (n + 4)) <= i) n += 4;

  serial_write(LOG_COM, '0');
  serial_write(LOG_COM, 'x');

  if (min_digits > 0) min_digits -= 1;
  min_digits <<= 2;

  while (min_digits > n) {
    serial_write(LOG_COM, '0');
    min_digits -= 4;
  }

  while (1) {
    uint32_t digit = (i >> n) & 0x0000000F;
    serial_write(LOG_COM, digits[digit]);

    if (n == 0) break;
    n -= 4;
  }
}

static void log_vprintf(char *fmt, va_list ap)
{
  char *p;
  uint32_t uival;
  int32_t ival;
  char *sval;

  for (p = fmt; *p != '\0'; ++p) {
    if (*p != '%') {
      serial_write(LOG_COM, *p);
      continue;
    }

    ++p;
    switch (*p) {
    case 'c':
      uival = va_arg(ap, uint32_t);
      serial_write(LOG_COM, (uint8_t) uival);
      break;
    case 'u':
      uival = va_arg(ap, uint32_t);
      log_ui(uival);
      break;
    case 'd':
      ival = va_arg(ap, int32_t);
      log_i(ival);
      break;
    case 'x':
      uival = va_arg(ap, uint32_t);
      log_hex(uival);
      break;
    case 's':
      sval = va_arg(ap, char*);
      for(; *sval; ++sval) {
        serial_write(LOG_COM, *sval);
      }
      break;
    case '%':
      serial_write(LOG_COM, '%');
      break;
    }
  }
}

static void log_printf(char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  log_vprintf(fmt, ap);
  va_end(ap);
}

void log_debug(char *fname, char *fmt, ...)
{
  va_list ap;
  log_printf("DEBUG: %s: ", fname);
  va_start(ap, fmt);
  log_vprintf(fmt, ap);
  va_end(ap);
}

void log_info(char *fname, char *fmt, ...)
{
  va_list ap;
  log_printf("INFO: %s: ", fname);
  va_start(ap, fmt);
  log_vprintf(fmt, ap);
  va_end(ap);
}

void log_error(char *fname, char *fmt, ...)
{
  va_list ap;
  log_printf("ERROR: %s: ", fname);
  va_start(ap, fmt);
  log_vprintf(fmt, ap);
  va_end(ap);
}
