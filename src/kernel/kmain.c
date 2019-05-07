
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/serial/serial.h>

void kmain()
{
  serial_init(SERIAL_COM1_BASE);
  serial_write(SERIAL_COM1_BASE, "hello", 5);
}
