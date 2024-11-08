
// kmain.c
//
// Kernel entry point.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "serial.h"
#include "ps2.h"
#include "tss.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "interrupt.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "fs.h"
#include "process.h"
#include "syscall.h"
#include "klock.h"
#include "elf.h"
#include "ata.h"
#include "ustar.h"
#include "fpu.h"
#include "ui.h"
#include "multiboot.h"
#include "constants.h"
#include "log.h"
#include "util.h"

#define STACK_CHK_GUARD 0xe2dee396

uintptr_t __stack_chk_guard = STACK_CHK_GUARD;
void __attribute__((noreturn)) __stack_chk_fail(void)
{ asm volatile ("xchg %bx, %bx"); while (1); }

void page_fault_handler(
  cpu_state_t cs, idt_info_t info, stack_state_t ss
  )
{
  uint32_t cr2;
  asm volatile ("movl %%cr2, %0" : "=r"(cr2));
  log_error(
    "kmain", "eip %x: page fault %x vaddr %x\n",
    ss.eip, info.error_code, cr2
    );
}

uint32_t debug_write(fs_node_t *n, size_t offset, size_t size, uint8_t *buf)
{
  log_debug("procdebug", "%u: %s", process_current()->pid, (char *)buf);
  return size;
}

#define CHECK(err, name) if ((err)) {                           \
    log_error("kmain", "Failed to initialize " name "\n");      \
  }                                                             \

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

  serial_init(SERIAL_COM1_BASE);
  fpu_init();
  tss_init();
  uint32_t tss_vaddr = tss_get_vaddr();
  gdt_init(tss_vaddr);
  idt_init();
  pic_init();
  pit_init();

  register_interrupt_handler(14, page_fault_handler);

  uint32_t res;
  res = pmm_init(mb_info, kphys_start, kphys_end);
  CHECK(res, "pmm");

  res = paging_init(kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR);
  CHECK(res, "paging");
  paging_set_kernel_pd(kernel_pd, (uint32_t)kernel_pd - KERNEL_START_VADDR);

  res = fs_init();
  CHECK(res, "fs");
  res = ata_init();
  CHECK(res, "ata");
  res = ustar_init("/dev/hda");
  CHECK(res, "ustar");
  res = ps2_init();
  CHECK(res, "ps2");

  uint32_t video_vaddr = paging_next_vaddr(768, KERNEL_START_VADDR);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t r;
  for (uint32_t i = 0; i < 768; ++i) {
    r = paging_map(video_vaddr + (i << 12), 0xFD000000 + (i << 12), flags);
    CHECK(r != PAGING_OK, "ui");
  }
  res = ui_init(video_vaddr);
  CHECK(res, "ui");

  fs_node_t *debug_node = kmalloc(sizeof(fs_node_t));
  u_memset(debug_node, 0, sizeof(fs_node_t));
  debug_node->write = debug_write;
  res = fs_mount(debug_node, "/dev/debug");
  CHECK(res, "debug_node");

  fs_node_t *null_node = kmalloc(sizeof(fs_node_t));
  u_memset(null_node, 0, sizeof(fs_node_t));
  res = fs_mount(null_node, "/dev/null");
  CHECK(res, "null_node");

  fs_node_t init_node;
  res = fs_open_node(&init_node, "/bin/init", 0);
  CHECK(res, "init");
  uint8_t *init_text = kmalloc(init_node.length);
  fs_read(&init_node, 0, init_node.length, init_text);

  unregister_interrupt_handler(14);
  res = process_init();
  CHECK(res, "process");

  process_image_t p;
  res = elf_load(&p, init_text);
  CHECK(res, "init ELF");

  process_create_schedule_init(p);

  kfree(init_text);
  kfree(p.text);
  kfree(p.data);

  enable_interrupts();
}
