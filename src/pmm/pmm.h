
// pmm.h
//
// Physical memory manager for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PMM_H_
#define _PMM_H_

#include <stdint.h>
#include <common/multiboot.h>

// Initialize the physical memory manager.
// Uses the memory map provided by GRUB to determine which
// regions of memory are available initially.
uint32_t pmm_init(
  multiboot_info_t *,
  const uint32_t, const uint32_t,
  const uint32_t, const uint32_t
  );

// Allocate multiple contiguous physical pages. Returns a physical
// start address or 0 if no pages are free.
uint32_t pmm_alloc(uint32_t);

// Free multiple contiguous physical pages. Takes a physical start
// address and number of pages.
void pmm_free(uint32_t, uint32_t);

#endif /* _PMM_H_ */
