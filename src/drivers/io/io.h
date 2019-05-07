
#ifndef _IO_H_
#define _IO_H_

// Send data to an I/O port. Defined in io.s.
void outb(unsigned short port, unsigned char data);

// Read a byte from an I/O port. Defined in io.s.
unsigned char inb(unsigned short port);

#endif /* _IO_H_ */
