
// serial.h
//
// Serial port driver for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SERIAL_H_
#define _SERIAL_H_

// COM1 base port.
extern const unsigned short SERIAL_COM1_BASE;

// Set the baud rate i.e the speed of the data being sent.
// The default speed of a serial port is 115200 bits/s.
// This function changes it to (115200 / `divisor`) bits/s.
void serial_configure_baud_rate(const unsigned short com, const unsigned short divisor);

// Write a byte to a serial port.
void serial_write(unsigned short com, char data);

// Initialise serial port with default configuration.
void serial_init(const unsigned short com);

#endif /* _SERIAL_H_ */
