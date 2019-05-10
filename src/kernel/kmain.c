
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/keyboard/keyboard.h>
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <interrupt/interrupt.h>

void kmain()
{
  disable_interrupts();

  gdt_init();
  idt_init();
  pic_init();

  pic_mask(1, 0); // Ignore timer interrupts for now.
  fb_clear();
  keyboard_init();

  enable_interrupts();
}
