
// paging.c
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include "paging.h"

static const uint32_t TABLE_ADDR_OFFSET = 12;

static page_directory_t *kernel_pd = 0;

// Initialize paging.
uint32_t paging_init(page_directory_t *pd)
{
  kernel_pd = pd;

  // We map the last entry in the page directory to the page directory
  // itself. This is allowed because the PD looks no different than a
  // page table.
  // Why do this? `cr3` has the physical address of the page directory,
  // which isn't very useful. It's nice to be able to access the PD at
  // virtual address 0xFFFFF000 at any time. This becomes especially
  // relevant when each process gets its own page directory, which can
  // be anywhere in memory.

  uint32_t kernel_pd_physaddr = (uint32_t)kernel_pd - KERNEL_START_VADDR;
  page_directory_entry_t self_pde;
  u_memset(&self_pde, 0, sizeof(self_pde));
  self_pde.present = 1;
  self_pde.rw = 1;
  self_pde.table_addr = kernel_pd_physaddr >> TABLE_ADDR_OFFSET;
  kernel_pd->entries[1023] = self_pde;

  return 0;
}
