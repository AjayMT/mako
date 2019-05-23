
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
#include <debug/log.h>
#include <util/util.h>

void page_fault_handler()
{
  log_debug("kmain", "page fault");
}

void kmain(
  uint32_t mb_info_addr,
  uint32_t mb_magic_number,
  page_directory_t kernel_pd,
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

  if (mb_magic_number != MULTIBOOT_BOOTLOADER_MAGIC) {
    log_error("kmain", "Incorrect magic number.");
    return;
  }

  interrupt_init();
  disable_interrupts();

  fb_clear();
  serial_init(SERIAL_COM1_BASE);
  gdt_init();
  idt_init();
  pic_init();
  keyboard_init();

  pic_mask(1, 0); // Ignore timer interrupts for now.

  register_interrupt_handler(14, page_fault_handler);

  uint32_t res;
  res = pmm_init(mb_info, kphys_start, kphys_end);
  if (res == 0) fb_write(" pmm", 4);

  res = paging_init(
    kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR
    );
  if (res == 0) fb_write(" pg", 3);

  page_directory_t pd = (page_directory_t)0xFFFFF000;
  page_directory_entry_t kern_pde = pd[KERNEL_PD_IDX];
  if (kern_pde.present) fb_write(" rec", 4);

  int *vaddr = (int *)(0xDEADBEEF & 0xFFFFF000);
  uint32_t paddr = pmm_alloc();
  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  // flags.rw = 1;
  paging_map((uint32_t)vaddr, paddr, flags);
  *vaddr = 12; // why no page fault?
  log_debug("kmain", "*vaddr: %u\n", *vaddr);

  char out[1024];
  for (uint32_t i = 0; i < 1024; ++i) {
    page_table_entry_t pde = (
      (page_table_t)
      (0xFFC00000 + (((uint32_t)vaddr) >> 22) * 0x1000)
      )[i];
    out[i] = 97 - (pde.present + (pde.rw * 2));
  }
  log_debug("kmain", "page table (` is present, ^ is present and writable):\n%s", out);

  enable_interrupts();
}
