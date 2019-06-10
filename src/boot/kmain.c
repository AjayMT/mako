
#include <drivers/framebuffer/framebuffer.h>
#include <drivers/serial/serial.h>
#include <drivers/keyboard/keyboard.h>
#include <tss/tss.h>
#include <gdt/gdt.h>
#include <idt/idt.h>
#include <pic/pic.h>
#include <interrupt/interrupt.h>
#include <paging/paging.h>
#include <pmm/pmm.h>
#include <kheap/kheap.h>
#include <fs/fs.h>
#include <rd/rd.h>
#include <common/multiboot.h>
#include <common/constants.h>
#include <debug/log.h>
#include <util/util.h>

void page_fault_handler(
  cpu_state_t cs, idt_info_t info, stack_state_t ss
  )
{
  uint32_t vaddr;
  asm("movl %%cr2, %0" : "=r"(vaddr));
  log_error(
    "kmain", "%u: page fault %x vaddr %x\n", info.idt_index, info.error_code, vaddr
    );
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

  if ((mb_info->flags & 8) == 0 || mb_info->mods_count != 1) {
    log_error("kmain", "Modules not loaded.");
    return;
  }

  multiboot_module_t *rd_module = (multiboot_module_t *)(mb_info->mods_addr);
  uint32_t rd_phys_start = rd_module->mod_start;
  uint32_t rd_phys_end = rd_module->mod_end;

  if (mb_magic_number != MULTIBOOT_BOOTLOADER_MAGIC) {
    log_error("kmain", "Incorrect magic number.");
    return;
  }

  interrupt_init();
  enable_interrupts();
  uint32_t eflags = interrupt_save_disable();

  fb_clear();
  serial_init(SERIAL_COM1_BASE);

  tss_init();
  uint32_t tss_vaddr = tss_get_vaddr();
  gdt_init(tss_vaddr);
  idt_init();
  pic_init();
  keyboard_init();

  pic_mask(1, 0); // Ignore timer interrupts for now.

  register_interrupt_handler(14, page_fault_handler);

  uint32_t res;
  res = pmm_init(mb_info, kphys_start, kphys_end, rd_phys_start, rd_phys_end);
  if (res == 0) fb_write(" pmm", 4);

  res = paging_init(
    kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR
    );
  if (res == 0) fb_write(" pg", 3);

  fs_init();
  res = rd_init(rd_phys_start, rd_phys_end);
  if (res == 0) fb_write(" rd", 3);

  fs_node_t *file = fs_open_node("/rd/hello.txt", 0);
  fs_node_t *file2 = fs_open_node("/rd/dir/file.txt", 0);

  uint8_t *buf = kmalloc(12);
  u_memset(buf, 0, 12);
  uint32_t size = fs_read(file, 0, 6, buf);
  fb_write((char *)buf, size);

  u_memset(buf, 0, 12);
  size = fs_read(file2, 0, 6, buf);
  fb_write((char *)buf, size);

  fs_node_t *dir = fs_open_node("/rd/dir", 0);
  struct dirent *ent = fs_readdir(dir, 2);
  fb_write(ent->name, u_strlen(ent->name));

  kfree(buf);
  kfree(file);
  kfree(file2);
  kfree(ent);
  kfree(dir);

  interrupt_restore(eflags);
}
