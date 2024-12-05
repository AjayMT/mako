
// kmain.c
//
// Kernel entry point.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "ata.h"
#include "constants.h"
#include "elf.h"
#include "fpu.h"
#include "fs.h"
#include "gdt.h"
#include "idt.h"
#include "interrupt.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "multiboot.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "process.h"
#include "ps2.h"
#include "serial.h"
#include "syscall.h"
#include "tss.h"
#include "ui.h"
#include "ustar.h"
#include "util.h"

#define STACK_CHK_GUARD 0xe2dee396

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;
void __attribute__((noreturn)) __stack_chk_fail(void)
{
  asm volatile("xchg %bx, %bx");
  while (1)
    ;
}

void page_fault_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  uint32_t cr2;
  asm volatile("movl %%cr2, %0" : "=r"(cr2));
  log_error("kmain", "eip %x: page fault %x vaddr %x\n", ss.eip, info.error_code, cr2);
}

uint32_t debug_write(fs_node_t *n, uint32_t offset, uint32_t size, uint8_t *buf)
{
  log_debug("procdebug", "%u: %s", process_current()->pid, (char *)buf);
  return size;
}

#define CHECK(err, name)                                                                           \
  if ((err)) {                                                                                     \
    log_error("kmain", "Failed to initialize " name "\n");                                         \
  }

void kmain(uint32_t mb_info_addr,
           uint32_t mb_magic_number,
           page_directory_t kernel_pd,
           uint32_t kvirt_start,
           uint32_t kvirt_end)
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

  serial_init(SERIAL_COM1_BASE);
  fpu_init();
  tss_init();
  uint32_t tss_vaddr = tss_get_vaddr();
  gdt_init(tss_vaddr);
  idt_init();
  pic_init();
  pit_init();

  register_interrupt_handler(14, page_fault_handler);

  uint32_t err;
  err = pmm_init(mb_info, kphys_start, kphys_end);
  CHECK(err, "pmm");

  err = paging_init(kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR);
  CHECK(err, "paging");
  paging_set_kernel_pd(kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR);

  err = fs_init();
  CHECK(err, "fs");
  err = ata_init();
  CHECK(err, "ata");
  err = ustar_init("/dev/hda");
  CHECK(err, "ustar");
  err = ps2_init();
  CHECK(err, "ps2");

  uint8_t *vbe_mode_info_p = (uint8_t *)(mb_info->vbe_mode_info + KERNEL_START_VADDR);
  // FIXME use a real vbe mode info struct
  const uint32_t video_frame_buffer_addr = *(uint32_t *)(vbe_mode_info_p + 40);
  log_info("kmain", "video frame buffer addr = %x\n", video_frame_buffer_addr);

  const uint32_t num_video_pages = (SCREENWIDTH * SCREENHEIGHT * sizeof(uint32_t)) >> 12;
  uint32_t video_vaddr = paging_next_vaddr(num_video_pages, KERNEL_START_VADDR);
  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t r;
  for (uint32_t i = 0; i < num_video_pages; ++i) {
    r = paging_map(video_vaddr + (i << 12), video_frame_buffer_addr + (i << 12), flags);
    CHECK(r != PAGING_OK, "ui");
  }
  err = ui_init(video_vaddr);
  CHECK(err, "ui");

  static fs_node_t debug_node;
  u_memset(&debug_node, 0, sizeof(fs_node_t));
  debug_node.write = debug_write;
  err = fs_mount(&debug_node, "/dev/debug");
  CHECK(err, "debug_node");

  static fs_node_t null_node;
  u_memset(&null_node, 0, sizeof(fs_node_t));
  err = fs_mount(&null_node, "/dev/null");
  CHECK(err, "null_node");

  fs_node_t init_node;
  err = fs_open_node(&init_node, "/bin/init", 0);
  CHECK(err, "init");
  uint8_t *init_text = kmalloc(init_node.length);
  fs_read(&init_node, 0, init_node.length, init_text);

  unregister_interrupt_handler(14);
  err = process_init();
  CHECK(err, "process");

  process_image_t p;
  err = elf_load(&p, init_text);
  CHECK(err, "init ELF");

  process_create_schedule_init(p);

  kfree(init_text);
  kfree(p.text);
  kfree(p.data);

  log_info("kmain", "kernel init complete\n");

  enable_interrupts();
}
