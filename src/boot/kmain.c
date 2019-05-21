
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/keyboard/keyboard.h>
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <interrupt/interrupt.h>
#include <paging/paging.h>

void kmain(page_directory_t *kernel_pd, page_table_t *kernel_pt)
{
  disable_interrupts();

  gdt_init();
  idt_init();
  pic_init();

  pic_mask(1, 0); // Ignore timer interrupts for now.
  fb_clear();
  keyboard_init();

  paging_init(kernel_pd, kernel_pt);
  
  enable_interrupts();
}
