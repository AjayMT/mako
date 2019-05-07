
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
static inline unsigned short serial_fifo_command_port(const unsigned short base)
{ return base + 2; }
static inline unsigned short serial_line_command_port(const unsigned short base)
{ return base + 3; }
static inline unsigned short serial_modem_command_port(const unsigned short base)
{ return base + 4; }
static inline unsigned short serial_line_status_port(const unsigned short base)
{ return base + 5; }

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
  outb(serial_line_command_port(com), SERIAL_LINE_ENABLE_DLAB);
  outb(com, (divisor >> 8) & 0x00FF);
  outb(com, divisor & 0x00FF);
}

// Check whether the transmit FIFO queue is empty for a given com.
// If bit 5 of the line status port is 1, the queue is empty.
static inline int serial_is_transmit_fifo_empty(const unsigned int com)
{
  return inb(serial_line_status_port(com)) & 0x20;
}

// Write a byte to a serial port.
static void serial_write_byte(
  const unsigned short com, const unsigned char byte
  )
{
  while (!serial_is_transmit_fifo_empty(com)); // wait until it's empty
  outb(com, byte);
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

  // Configure line -- set the port to have a data length of 8 bytes
  // and some other stuff.
  // See https://en.wikipedia.org/wiki/Serial_port#Settings
  outb(serial_line_command_port(com), 0x03);

  // Configure modem -- set RTS and DTR to be 1.
  // See https://en.wikipedia.org/wiki/Serial_port#Hardware_abstraction
  outb(serial_modem_command_port(com), 0x03);

  // Configure buffer -- enable FIFO, clear both receiver and
  // transmission FIFO queues, set queue size to 14 bytes.
  outb(serial_fifo_command_port(com), 0xC7);
}
