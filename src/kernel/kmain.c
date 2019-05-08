
#include <common/stdint.h>
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/serial/serial.h>
#include <gdt/gdt.h>
#include <idt/idt.h>

void kmain()
{
  gdt_init();
  idt_init();
}
