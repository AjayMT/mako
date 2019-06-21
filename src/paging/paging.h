
// paging.h
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PAGING_H_
#define _PAGING_H_

#include <stdint.h>

// A single page table entry.
// Bit-fields with descriptions ending in `?` are flags that we set,
// those ending in '.' are flags that the CPU sets.
struct page_table_entry_s {
  uint32_t present    : 1;  // Present in memory?
  uint32_t rw         : 1;  // Writable?
  uint32_t user       : 1;  // Accessible in user mode?
  uint32_t pwt        : 1;  // Write through cache?
  uint32_t pcd        : 1;  // Disable caching?
  uint32_t accessed   : 1;  // Page frame was accessed.
  uint32_t dirty      : 1;  // Page frame was modified.
  uint32_t unused     : 5;
  uint32_t frame_addr : 20; // Only the upper 20 bits.
} __attribute__((packed));
typedef struct page_table_entry_s page_table_entry_t;
typedef page_table_entry_t *page_table_t;

// A single page directory entry.
// Page directory entries can point to page tables or to 4MB
// pages if the PSE (page size extension) bit in `cr4` is set.
struct page_directory_entry_s {
  uint32_t present    : 1;  // Present in memory?
  uint32_t rw         : 1;  // Writable?
  uint32_t user       : 1;  // Accessible in user mode?
  uint32_t pwt        : 1;  // Write through cache?
  uint32_t pcd        : 1;  // Disable caching?
  uint32_t accessed   : 1;  // Frame/table was accessed.
  uint32_t dirty      : 1;  // Frame/table was modified.
  uint32_t page_size  : 1;  // Is a 4MB page frame?
  uint32_t unused     : 4;
  uint32_t table_addr : 20; // Only the upper 20 bits.
} __attribute__((packed));
typedef struct page_directory_entry_s page_directory_entry_t;
typedef page_directory_entry_t *page_directory_t;

// Result of paging_map and paging_unmap.
typedef enum {
  PAGING_OK,
  PAGING_MAP_EXISTS,
  PAGING_NO_MEMORY,
  PAGING_NO_ACCESS
} paging_result_t;

// Initialize paging.
uint32_t paging_init(page_directory_t, uint32_t);

// Set/get kernel page directory address.
void paging_set_kernel_pd(page_directory_t, uint32_t);
void paging_get_kernel_pd(page_directory_t *, uint32_t *);

// Clone a process's page directory. Takes and returns physical
// addresses.
uint32_t paging_clone_process_directory(uint32_t *, uint32_t);

// Clear the user-mode address space.
uint8_t paging_clear_user_space();

// Shallow copy the kernel's address space. Takes the physical
// address of the page directory to copy into.
uint32_t paging_copy_kernel_space(uint32_t);

// Implemented in paging.s.
void paging_set_cr3(uint32_t);
uint32_t paging_get_cr3();

// Remove a PTE from the TLB. Implemented in paging.s.
void paging_invalidate_pte(uint32_t);

// Map a virtual page starting at `virt_addr` to a physical page
// starting at `phys_addr`. `flags` specifies .rw, .user, .pwt and
// other PTE flags.
paging_result_t paging_map(
  uint32_t virt_addr, uint32_t phys_addr, page_table_entry_t flags
  );

// Unmap a page starting at virtual address `virt_addr`.
paging_result_t paging_unmap(uint32_t virt_addr);

// Get the next free virtual address at the start of multiple
// contiguous unmapped pages.
uint32_t paging_next_vaddr(uint32_t, uint32_t);

// Get the last free virtual address at the start of multiple
// contiguous unmapped pages.
uint32_t paging_prev_vaddr(uint32_t, uint32_t);

// Get the physical address that a virtual address is mapped to.
uint32_t paging_get_paddr(uint32_t);

#endif /* _PAGING_H_ */
