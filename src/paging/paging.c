
// paging.c
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <pmm/pmm.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include "paging.h"

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

  paging_set_directory(phys_addr);

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
    if (pt_paddr == 0) // Unable to allocate new page.
      return PAGING_NO_MEMORY;

    u_memset(&pde, 0, sizeof(pde));
    pde.present = 1;
    pde.rw = 1;
    pde.user = flags.user;
    pde.pwt = flags.pwt;
    pde.pcd = flags.pcd;
    pde.table_addr = pt_paddr >> PHYS_ADDR_OFFSET;
    pd[pd_idx] = pde;
    u_memset((void *)pt_vaddr, 0, PAGE_SIZE); // Clear the new page table.
    pd[pd_idx].rw = flags.rw;
  }

  page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
  uint32_t pt_idx = vaddr_to_pt_idx(virt_addr);
  page_table_entry_t pte = pt[pt_idx];

  if (pte.present) return PAGING_MAP_EXISTS; // Page already mapped.

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
  if (
    pd_idx == vaddr_to_pd_idx(KERNEL_START_VADDR)
    || pd_idx == vaddr_to_pd_idx(PD_VADDR)
    )
    return PAGING_NO_ACCESS;

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

// Get the next free virtual address.
uint32_t paging_next_vaddr()
{
  page_directory_t pd = (page_directory_t)PD_VADDR;
  for (uint32_t pd_idx = 0; pd_idx < PAGE_SIZE_DWORDS; ++pd_idx) {
    page_directory_entry_t pde = pd[pd_idx];
    uint32_t pt_idx = 0;
    if (pde.present == 0) {
      if (pd_idx == 0) ++pt_idx; // Don't return address 0x0.
      return pd_idx_to_vaddr(pd_idx) | pt_idx_to_vaddr(pt_idx);
    }

    page_table_t pt = (page_table_t)pd_idx_to_pt_vaddr(pd_idx);
    for (; pt_idx < PAGE_SIZE_DWORDS; ++pt_idx) {
      // Skip mapped pages and address 0x0.
      if (pt[pt_idx].present || (pd_idx == 0 && pt_idx == 0))
        continue;

      return pd_idx_to_vaddr(pd_idx) | pt_idx_to_vaddr(pt_idx);
    }
  }

  return 0;
}
