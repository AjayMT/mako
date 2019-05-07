
// serial.c
//
// Serial port driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <drivers/io/io.h>
#include "serial.h"

// I/O ports.
// All I/O ports are calculated relative to the data port.
const unsigned short SERIAL_COM1_BASE = 0x3F8; // COM1 base port.
#define SERIAL_DATA_PORT(base)          (base)
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)

// I/O port commands.
// Tells the serial port to expect first the highest 8 bits on the data port,
// then the lowest 8 bits will follow.
static const unsigned short SERIAL_LINE_ENABLE_DLAB = 0x80;

// Set the baud rate i.e the speed of the data being sent.
// The default speed of a serial port is 115200 bits/s.
// This function changes it to (115200 / `divisor`) bits/s.
void serial_configure_baud_rate(
  const unsigned short com, const unsigned short divisor
  )
{
  outb(SERIAL_LINE_COMMAND_PORT(com), SERIAL_LINE_ENABLE_DLAB);
  outb(SERIAL_DATA_PORT(com), (divisor >> 8) & 0x00FF);
  outb(SERIAL_DATA_PORT(com), divisor & 0x00FF);
}

// Configure the line of the given serial port.
// See https://en.wikipedia.org/wiki/Serial_port#Settings
// We set the port to have a data length of 8 bits and some other stuff.
static inline void serial_configure_line(const unsigned short com)
{
  outb(SERIAL_LINE_COMMAND_PORT(com), 0x03);
}

// Configure the buffer of the given serial port.
// We enable FIFO, clear both the receiver and transmission FIFO queues
// and set the queue size to 14 bytes.
static inline void serial_configure_buffer(const unsigned short com)
{
  outb(SERIAL_FIFO_COMMAND_PORT(com), 0xC7);
}

// Configure the modem of the given serial port.
// See https://en.wikipedia.org/wiki/Serial_port#Hardware_abstraction
// We set RTS and DTR to be 1.
static inline void serial_configure_modem(const unsigned short com)
{
  outb(SERIAL_MODEM_COMMAND_PORT(com), 0x03);
}

// Check whether the transmit FIFO queue is empty for a given com.
// If bit 5 of the line status port is 1, the queue is empty.
static inline int serial_is_transmit_fifo_empty(const unsigned int com)
{
  return inb(SERIAL_LINE_STATUS_PORT(com)) & 0x20;
}

// Write a byte to a serial port.
static void serial_write_byte(
  const unsigned short com, const unsigned char byte
  )
{
  while (!serial_is_transmit_fifo_empty(com)); // wait until it's empty
  outb(SERIAL_DATA_PORT(com), byte);
}

// Write a sequence of bytes of length `len` to a serial port.
void serial_write(
  unsigned short com, const char *data, const unsigned int len
  )
{
  for (unsigned int i = 0; i < len; ++i)
    serial_write_byte(com, data[i]);
}

// Initialize serial port with default configuration.
void serial_init(const unsigned short com)
{
  serial_configure_baud_rate(com, 3);
  serial_configure_line(com);
  serial_configure_modem(com);
  serial_configure_buffer(com);
}
