
// paging.c
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <pmm/pmm.h>
#include <interrupt/interrupt.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include <common/errno.h>
#include "paging.h"

#define CHECK(err, msg, code) if ((err)) {          \
    log_error("paging", msg "\n"); return (code);   \
  }

static page_directory_t kernel_pd_vaddr = 0;
static uint32_t kernel_pd_paddr = 0;

static inline uint32_t vaddr_to_pd_idx(uint32_t vaddr)
{ return vaddr >> 22; }
static inline uint32_t vaddr_to_pt_idx(uint32_t vaddr)
{ return (vaddr >> 12) & 0x3FF; }
static inline uint32_t pd_idx_to_vaddr(uint32_t pd_idx)
{ return pd_idx << 22; }
static inline uint32_t pt_idx_to_vaddr(uint32_t pt_idx)
{ return pt_idx << 12; }
static inline uint32_t pd_idx_to_pt_vaddr(uint32_t pd_idx)
{ return FIRST_PT_VADDR + (PAGE_SIZE * pd_idx); }

// Initialize paging.
uint32_t paging_init(page_directory_t pd, uint32_t phys_addr)
{
  uint32_t eflags = interrupt_save_disable();

  // We map the last entry in the page directory to the page directory
  // itself. This is allowed because the PD looks no different than a
  // page table.
  // Why do this? `cr3` has the physical address of the page directory,
  // which isn't very useful. It's nice to be able to access the PD at
  // virtual address 0xFFFFF000 at any time. This becomes especially
  // relevant when each process gets its own page directory, which can
  // be anywhere in memory.
  page_directory_entry_t self_pde;
  u_memset(&self_pde, 0, sizeof(self_pde));
  self_pde.present = 1;
  self_pde.rw = 1;
  self_pde.table_addr = phys_addr >> PHYS_ADDR_OFFSET;
  pd[vaddr_to_pd_idx(PD_VADDR)] = self_pde;

  // Map the kernel (i.e first 4MB of the physical address space)
  // to a 4MB page. The kernel is mapped in every process's address space
  // so the page directory doesn't have to be switched when making a syscall.
  page_directory_entry_t kern_pde;
  u_memset(&kern_pde, 0, sizeof(kern_pde));
  kern_pde.present = 1;
  kern_pde.rw = 1;
  kern_pde.page_size = 1;
  pd[vaddr_to_pd_idx(KERNEL_START_VADDR)] = kern_pde;

  paging_set_cr3(phys_addr);

  interrupt_restore(eflags);
  return 0;
}

// Set/get kernel page directory address.
void paging_set_kernel_pd(page_directory_t vaddr, uint32_t paddr)
{ kernel_pd_vaddr = vaddr; kernel_pd_paddr = paddr; }
void paging_get_kernel_pd(page_directory_t *vaddr, uint32_t *paddr)
{ *vaddr = kernel_pd_vaddr; *paddr = kernel_pd_paddr; }

#define CHECK_UNLOCK(err, msg, code) if ((err)) {               \
    log_error("paging", msg "\n"); interrupt_restore(eflags);   \
    return (code);                                              \
  }

// Shallow copy the kernel's address space.
uint32_t paging_copy_kernel_space(uint32_t cr3)
{
  uint32_t eflags = interrupt_save_disable();

  // Map the page directory to an address in user space so it
  // doesn't get copied.
  uint32_t pd_vaddr = paging_next_vaddr(1, 0);
  CHECK_UNLOCK(pd_vaddr == 0, "No memory.", ENOMEM);
  page_directory_t pd = (page_directory_t)pd_vaddr;
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(pd_vaddr, cr3, flags);
  CHECK_UNLOCK(res != PAGING_OK, "paging_map failed.", res);

  uint32_t start_idx = vaddr_to_pd_idx(KERNEL_START_VADDR);
  page_directory_t current_pd = (page_directory_t)PD_VADDR;
  for (uint32_t pd_idx = start_idx; pd_idx < PAGE_SIZE_DWORDS - 1; ++pd_idx)
    pd[pd_idx] = current_pd[pd_idx];

  page_directory_entry_t self_pde;
  u_memset(&self_pde, 0, sizeof(self_pde));
  self_pde.present = 1;
  self_pde.rw = 1;
  self_pde.table_addr = cr3 >> PHYS_ADDR_OFFSET;
  pd[vaddr_to_pd_idx(PD_VADDR)] = self_pde;

  res = paging_unmap(pd_vaddr);
  CHECK_UNLOCK(res != PAGING_OK, "paging_unmap failed.", res);

  interrupt_restore(eflags);
  return 0;
}

#define CHECK_RESTORE(err, msg, code) if ((err)) {              \
    log_error("paging", msg "\n"); interrupt_restore(eflags);   \
    paging_set_cr3(current_cr3); return (code);                 \
  }

// Clone a process' page directory.
uint32_t paging_clone_process_directory(
  uint32_t *out_cr3, uint32_t process_cr3
  )
{
  uint32_t eflags = interrupt_save_disable();
  uint32_t current_cr3 = paging_get_cr3();

  paging_set_cr3(process_cr3);
  page_directory_t process_pd = (page_directory_t)PD_VADDR;

  uint32_t cr3 = pmm_alloc(1);
  CHECK_RESTORE(cr3 == 0, "No memory.", ENOMEM);
  CHECK_RESTORE(
    paging_copy_kernel_space(cr3),
    "Could not copy kernel address space.",
    1
    );

  uint32_t pd_vaddr = paging_next_vaddr(1, KERNEL_START_VADDR);
  CHECK_RESTORE(pd_vaddr == 0, "No memory.", ENOMEM);
  page_directory_t pd = (page_directory_t)pd_vaddr;
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(pd_vaddr, cr3, flags);
  CHECK_RESTORE(res != PAGING_OK, "paging_map failed.", 1);

  uint32_t kernel_idx = vaddr_to_pd_idx(KERNEL_START_VADDR);
  for (uint32_t pd_idx = 0; pd_idx < kernel_idx; ++pd_idx) {
    pd[pd_idx] = process_pd[pd_idx];
    if (pd[pd_idx].present == 0) continue;

    page_table_t process_pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
    uint32_t pt_paddr = pmm_alloc(1);
    CHECK_RESTORE(pt_paddr == 0, "No memory.", ENOMEM);
    uint32_t pt_vaddr = paging_next_vaddr(1, KERNEL_START_VADDR);
    CHECK_RESTORE(pt_vaddr == 0, "No memory.", ENOMEM);
    page_table_t pt = (page_table_t)pt_vaddr;
    res = paging_map(pt_vaddr, pt_paddr, flags);
    CHECK_RESTORE(res != PAGING_OK, "paging_map failed.", 1);
    pd[pd_idx].table_addr = pt_paddr >> PHYS_ADDR_OFFSET;

    for (uint32_t pt_idx = 0; pt_idx < PAGE_SIZE_DWORDS; ++pt_idx) {
      pt[pt_idx] = process_pt[pt_idx];
      if (pt[pt_idx].present == 0) continue;

      uint32_t frame_paddr = pmm_alloc(1);
      CHECK_RESTORE(frame_paddr == 0, "No memory.", ENOMEM);
      uint32_t frame_vaddr = paging_next_vaddr(1, KERNEL_START_VADDR);
      CHECK_RESTORE(frame_vaddr == 0, "No memory.", ENOMEM);
      res = paging_map(frame_vaddr, frame_paddr, flags);
      CHECK_RESTORE(res != PAGING_OK, "paging_map failed.", 1);
      pt[pt_idx].frame_addr = frame_paddr >> PHYS_ADDR_OFFSET;
      u_memcpy(
        (uint8_t *)frame_vaddr,
        (uint8_t *)(pd_idx_to_vaddr(pd_idx) | pt_idx_to_vaddr(pt_idx)),
        PAGE_SIZE
        );
      res = paging_unmap(frame_vaddr);
      CHECK_RESTORE(res != PAGING_OK, "paging_unmap failed.", 1);
    }

    res = paging_unmap(pt_vaddr);
    CHECK_RESTORE(res != PAGING_OK, "paging_unmap failed.", 1);
  }

  res = paging_unmap(pd_vaddr);
  CHECK_RESTORE(res != PAGING_OK, "paging_unmap failed.", 1);
  paging_set_cr3(current_cr3);
  *out_cr3 = cr3;

  interrupt_restore(eflags);
  return 0;
}

// Clear the user-mode address space.
uint8_t paging_clear_user_space()
{
  page_directory_t pd = (page_directory_t)PD_VADDR;
  uint32_t kernel_pd_idx = vaddr_to_pd_idx(KERNEL_START_VADDR);
  for (uint32_t pd_idx = 0; pd_idx < kernel_pd_idx; ++pd_idx) {
    if (pd[pd_idx].present == 0) continue;

    page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
    for (uint32_t pt_idx = 0; pt_idx < PAGE_SIZE_DWORDS; ++pt_idx) {
      if (pd[pd_idx].present == 0) break;
      if (pt[pt_idx].present == 0) continue;

      uint32_t vaddr = pd_idx_to_vaddr(pd_idx) | pt_idx_to_vaddr(pt_idx);
      pmm_free(paging_get_paddr(vaddr), 1);
      paging_result_t res = paging_unmap(vaddr);
      CHECK(res != PAGING_OK, "paging_unmap failed.", 1);
    }
  }

  return 0;
}

// Map a page.
paging_result_t paging_map(
  uint32_t virt_addr, uint32_t phys_addr, page_table_entry_t flags
  )
{
  page_directory_t pd = (page_directory_t)PD_VADDR;
  uint32_t pd_idx = vaddr_to_pd_idx(virt_addr);
  page_directory_entry_t pde = pd[pd_idx];

  if (pde.present == 0) { // We have to make a new page table.
    uint32_t pt_vaddr = pd_idx_to_pt_vaddr(pd_idx);
    uint32_t pt_paddr = pmm_alloc(1);
    CHECK(pt_paddr == 0, "No memory.", PAGING_NO_MEMORY);

    u_memset(&pde, 0, sizeof(pde));
    pde.present = 1;
    pde.rw = 1;
    pde.user = flags.user;
    pde.pwt = flags.pwt;
    pde.pcd = flags.pcd;
    pde.table_addr = pt_paddr >> PHYS_ADDR_OFFSET;
    pd[pd_idx] = pde;
    u_memset((void *)pt_vaddr, 0b10, PAGE_SIZE); // Clear the new page table.
    pd[pd_idx].rw = flags.rw;
  }

  page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
  uint32_t pt_idx = vaddr_to_pt_idx(virt_addr);
  page_table_entry_t pte = pt[pt_idx];
  CHECK(pte.present, "Map already exists.", PAGING_MAP_EXISTS);

  u_memset(&pte, 0, sizeof(pte));
  pte.present = 1;
  pte.rw = flags.rw;
  pte.user = flags.user;
  pte.pwt = flags.pwt;
  pte.pcd = flags.pcd;
  pte.frame_addr = phys_addr >> PHYS_ADDR_OFFSET;
  pt[pt_idx] = pte;

  return PAGING_OK;
}

// Unmap a page.
paging_result_t paging_unmap(uint32_t virt_addr)
{
  uint32_t pd_idx = vaddr_to_pd_idx(virt_addr);

  // Cannot unmap kernel memory or page directory.
  CHECK(
    pd_idx == vaddr_to_pd_idx(KERNEL_START_VADDR)
    || pd_idx == vaddr_to_pd_idx(PD_VADDR),
    "Attempt to unmap kernel memory or page directory.",
    PAGING_NO_ACCESS
    );

  page_directory_t pd = (page_directory_t)PD_VADDR;
  page_directory_entry_t pde = pd[pd_idx];
  if (pde.present == 0) return PAGING_OK;

  page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
  uint32_t pt_idx = vaddr_to_pt_idx(virt_addr);
  pt[pt_idx].present = 0;
  paging_invalidate_pte(virt_addr);

  // Check if there are any present pages in the page table.
  for (
    pt_idx = 0;
    pt_idx < PAGE_SIZE_DWORDS && pt[pt_idx].present == 0;
    ++pt_idx
    );

  if (pt_idx == PAGE_SIZE_DWORDS) { // There aren't any.
    pmm_free(pde.table_addr << PHYS_ADDR_OFFSET, 1);
    pde.present = 0;
    pd[pd_idx] = pde;
    paging_invalidate_pte((uint32_t)pt);
  }

  return PAGING_OK;
}

// Get the next free virtual address at the start of multiple
// contiguous unmapped pages.
uint32_t paging_next_vaddr(uint32_t size, uint32_t base)
{
  page_directory_t pd = (page_directory_t)PD_VADDR;
  uint32_t max_page_number = PAGE_SIZE_DWORDS * PAGE_SIZE_DWORDS;
  uint32_t current = base >> PHYS_ADDR_OFFSET, step = 0;
  if (current == 0) ++current; // Skip address 0 so NULL is invalid.
  for (; current < max_page_number; current += step + 1) {
    uint32_t found = 0;
    for (step = 0; step < size; ++step) {
      uint32_t page_number = current + step;
      uint32_t pd_idx = page_number >> 10;
      uint32_t pt_idx = page_number & 0x3FF;
      page_directory_entry_t pde = pd[pd_idx];
      if (pde.present == 0) {
        found += PAGE_SIZE_DWORDS - pt_idx;
        if (found >= size) return current << PHYS_ADDR_OFFSET;
        continue;
      } else if (pde.page_size) break;

      page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
      page_table_entry_t pte = pt[pt_idx];
      if (pte.present == 0) ++found;
      if (found >= size) return current << PHYS_ADDR_OFFSET;
      if (pte.present) break;
    }
  }

  return 0;
}

// Get the last free virtual address at the start of multiple
// contiguous unmapped pages.
uint32_t paging_prev_vaddr(uint32_t size, uint32_t top)
{
  page_directory_t pd = (page_directory_t)PD_VADDR;
  uint32_t min_page_number = 1;
  uint32_t current = (top >> PHYS_ADDR_OFFSET) - 1, step = 0;
  for (; current >= min_page_number; current -= step + 1) {
    uint32_t found = 0;
    for (step = 0; step < size; ++step) {
      uint32_t page_number = current - step;
      uint32_t pd_idx = page_number >> 10;
      uint32_t pt_idx = page_number & 0x3FF;
      page_directory_entry_t pde = pd[pd_idx];
      if (pde.present == 0) {
        found += PAGE_SIZE_DWORDS - pt_idx;
        if (found >= size) return page_number << PHYS_ADDR_OFFSET;
        continue;
      } else if (pde.page_size) break;

      page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
      page_table_entry_t pte = pt[pt_idx];
      if (pte.present == 0) ++found;
      if (found >= size) return page_number << PHYS_ADDR_OFFSET;
      if (pte.present) break;
    }
  }

  return 0;
}

// Get the physical address that a virtual address is mapped to.
uint32_t paging_get_paddr(uint32_t vaddr)
{
  uint32_t pd_idx = vaddr_to_pd_idx(vaddr);
  uint32_t pt_idx = vaddr_to_pt_idx(vaddr);
  page_directory_t pd = (page_directory_t)PD_VADDR;
  if (pd[pd_idx].present == 0) return 0;
  if (pd[pd_idx].page_size == 1) {
    uint32_t aligned_down = vaddr & 0xFFFFF000000;
    uint32_t diff = vaddr - aligned_down;
    return (pd[pd_idx].table_addr << PHYS_ADDR_OFFSET) + diff;
  }

  page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
  if (pt[pt_idx].present == 0) return 0;
  uint32_t aligned_down = vaddr & 0xFFFFF000;
  uint32_t diff = vaddr - aligned_down;
  return (pt[pt_idx].frame_addr << PHYS_ADDR_OFFSET) + diff;
}
