
#include <common/stdint.h>
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/serial/serial.h>
#include <gdt/gdt.h>

void kmain()
{
  gdt_init();
}
