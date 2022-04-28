
// kheap.c
//
// Kernel heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "paging.h"
#include "pmm.h"
#include "interrupt.h"
#include "util.h"
#include "log.h"
#include "constants.h"
#include "kheap.h"

// This is a simple first-fit allocator with block splitting / coalescing.

static const uint16_t BLOCK_MAGIC = 0xC0FF;
static const size_t MIN_BLOCK_SIZE = 8;
static const uint32_t PAGE_FREE_THRESHOLD = 2 * PAGE_SIZE;

struct block_s {
  // The lowest bit of this field is a `free` flag.
  uint16_t magic_free;
  uint32_t size;
  struct block_s *next;
  struct block_s *prev;
} __attribute__((packed));
typedef struct block_s block_t;

static block_t *head = NULL;

// Check if two blocks are adjacent.
static inline uint8_t check_adjacent(block_t *left, block_t *right)
{ return (uint32_t)(left + 1) + left->size == (uint32_t)right; }

// Split a block into two adjacent blocks.
// The new block header is placed at `(uint32_t)(block + 1) + split_offset`.
static block_t *split_block(block_t *block, size_t split_offset)
{
  block_t new_block = {
    .magic_free = BLOCK_MAGIC,
    .size = block->size - (split_offset + sizeof(block_t)),
    .next = block->next,
    .prev = block
  };
  block_t *new_block_ptr = (block_t *)((uint32_t)(block + 1) + split_offset);
  *new_block_ptr = new_block;
  if (block->next) block->next->prev = new_block_ptr;
  block->next = new_block_ptr;
  block->size = split_offset;
  return new_block_ptr;
}

// Merge a block with the next block.
// This function does not check the `prev` flag and assumes that the blocks
// are adjacent to each other.
static block_t *merge_block(block_t *block)
{
  block_t *next_block = block->next;
  block->next = next_block->next;
  if (block->next) block->next->prev = block;
  block->size += next_block->size + sizeof(block_t);
  return block;
}

// Allocate enough pages to make a block with the given size.
// The `size` parameter does not include the size of the block header.
static block_t *alloc_pages(size_t size)
{
  size = u_page_align_up(size + sizeof(block_t));

  uint32_t npages = size >> PHYS_ADDR_OFFSET;
  uint32_t vaddr = paging_next_vaddr(npages, KERNEL_START_VADDR);
  if (vaddr == 0) return NULL;

  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  uint32_t acquired_size = 0;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    if (paddr == 0) break;
    paging_result_t res = paging_map(vaddr + acquired_size, paddr, flags);
    if (res != PAGING_OK) break;
    acquired_size += PAGE_SIZE;
  }

  if (acquired_size == 0) return NULL;

  block_t *new_block = (block_t *)vaddr;
  new_block->magic_free = BLOCK_MAGIC;
  new_block->next = head;
  new_block->prev = NULL;
  new_block->size = acquired_size - sizeof(block_t);
  if (head) head->prev = new_block;
  head = new_block;

  return new_block;
}

// Unmap and free any pages that are within `block`.
static void free_pages(block_t *block)
{
  uint32_t block_start = (uint32_t)block;
  uint32_t block_end = (uint32_t)(block + 1) + block->size;
  uint32_t page_start = u_page_align_up(block_start);
  uint32_t page_end = u_page_align_down(block_end);

  if (page_start - block_start < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_start += PAGE_SIZE;
  if (block_end - page_end < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_end -= PAGE_SIZE;

  if (page_start >= page_end || page_end - page_start < PAGE_FREE_THRESHOLD) return;

  block_t *removed_block = split_block(block, page_start - (block_start + sizeof(block_t)));
  split_block(removed_block, page_end - (page_start + sizeof(block_t)));
  block->next = removed_block->next;
  if (removed_block->next) removed_block->next->prev = block;

  for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
    pmm_free(paging_get_paddr(vaddr), 1);
    paging_unmap(vaddr);
  }
}

void *kmalloc(size_t size)
{
  if (size == 0) return NULL;
  if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;
  if (size & 1) ++size;

  // TODO: maybe use a klock instead of disabling interrupts?
  // The overhead of getting pre-empted while mallocing and blocking
  // other tasks from accessing the heap may outweigh the cost of disabling
  // scheduler interrupts.
  uint32_t eflags = interrupt_save_disable();

  block_t *current = head;
  while (current) {
    if ((current->magic_free & 1) && current->size >= size) break;
    current = current->next;
  }

  if (current == NULL) current = alloc_pages(size);
  if (current == NULL || current->size < size) {
    interrupt_restore(eflags);
    return NULL;
  }

  if (current->size - size > sizeof(block_t) + MIN_BLOCK_SIZE)
    split_block(current, size);

  current->magic_free &= ~1;

  interrupt_restore(eflags);

  return (void *)(current + 1);
}

void kfree(void *ptr)
{
  if (ptr == NULL) return;

  block_t *block = ptr;
  if (block->magic_free != (BLOCK_MAGIC & (~1))) return;

  uint32_t eflags = interrupt_save_disable();

  block->magic_free |= 1;
  if (block->prev && (block->prev->magic_free & 1) && check_adjacent(block->prev, block))
    block = merge_block(block->prev);

  if (block->next && (block->next->magic_free & 1) && check_adjacent(block, block->next))
    merge_block(block);

  if (block->size + sizeof(block_t) >= PAGE_SIZE) free_pages(block);

  interrupt_restore(eflags);
}
