
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/serial/serial.h>
#include <drivers/keyboard/keyboard.h>
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <interrupt/interrupt.h>
#include <paging/paging.h>
#include <pmm/pmm.h>
#include <common/multiboot.h>
#include <common/constants.h>

void kmain(
  uint32_t mb_info_addr,
  uint32_t mb_magic_number,
  page_directory_t *kernel_pd,
  uint32_t kvirt_start,
  uint32_t kvirt_end
  )
{
  // Convert phyical addresses in the multiboot info structure
  // into virtual addresses.
  multiboot_info_t *mb_info = (multiboot_info_t *)mb_info_addr;
  mb_info->mods_addr += KERNEL_START_VADDR;
  mb_info->mmap_addr += KERNEL_START_VADDR;

  // Convert virtual addresses exported by link.ld to physical
  // addresses.
  uint32_t kphys_start = kvirt_start - KERNEL_START_VADDR;
  uint32_t kphys_end = kvirt_end - KERNEL_START_VADDR;

  fb_clear();
  if (mb_magic_number != MULTIBOOT_BOOTLOADER_MAGIC)
    fb_write("merr ", 5);

  disable_interrupts();

  serial_init(SERIAL_COM1_BASE);
  gdt_init();
  idt_init();
  pic_init();

  pic_mask(1, 0); // Ignore timer interrupts for now.
  keyboard_init();

  uint32_t res = paging_init(kernel_pd);
  if (res == 0)
    fb_write(" pg", 3);

  res = pmm_init(mb_info, kphys_start, kphys_end);
  if (res == 0)
    fb_write(" pmm", 4);

  enable_interrupts();
}
