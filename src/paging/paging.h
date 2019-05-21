
// paging.h
//
// Paging interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PAGING_H_
#define _PAGING_H_

#include <stdint.h>

// A single page table entry.
struct page_table_entry_s {
  uint32_t present    : 1;
  uint32_t rw         : 1;
  uint32_t user       : 1;
  uint32_t pwt        : 1;
  uint32_t pcd        : 1;
  uint32_t accessed   : 1;
  uint32_t dirty      : 1;
  uint32_t unused     : 5;
  uint32_t frame_addr : 20;
} __attribute__((packed));
typedef struct page_table_entry_s page_table_entry_t;

struct page_table_s {
  page_table_entry_t entries[1024];
} __attribute__((packed));
typedef struct page_table_s page_table_t;

struct page_directory_entry_s {
  uint32_t present    : 1;
  uint32_t rw         : 1;
  uint32_t user       : 1;
  uint32_t pwt        : 1;
  uint32_t pcd        : 1;
  uint32_t accessed   : 1;
  uint32_t dirty      : 1;
  uint32_t page_size  : 1;
  uint32_t unused     : 4;
  uint32_t table_addr : 20;
} __attribute__((packed));
typedef struct page_directory_entry_s page_directory_entry_t;

struct page_directory_s {
  page_directory_entry_t entries[1024];
} __attribute__((packed));
typedef struct page_directory_s page_directory_t;

// Initialize paging.
void paging_init(page_directory_t *kernel_pd, page_table_t *kernel_pt);

#endif /* _PAGING_H_ */
