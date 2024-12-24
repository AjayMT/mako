
// kheap.c
//
// Kernel heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "kheap.h"
#include "../common/stdint.h"
#include "constants.h"
#include "interrupt.h"
#include "log.h"
#include "paging.h"
#include "pmm.h"
#include "util.h"
#include <stddef.h>

// This is a simple first-fit allocator with block splitting / coalescing.

static const size_t MIN_BLOCK_SIZE = 8;
static const uint32_t PAGE_FREE_THRESHOLD = PAGE_SIZE;

struct block_s
{
  // The size of each block is always an even number and the last bit
  // is a flag that indicates whether an adjacent next block exists.
  uint32_t size_next;
  struct block_s *prev;
  struct block_s *prev_free;
  struct block_s *next_free;
} __attribute__((packed));
typedef struct block_s block_t;

static block_t *head = NULL;

// Simple block properties.
static inline uint32_t size(block_t *block)
{
  return block->size_next & ~1;
}
static inline block_t *next(block_t *block)
{
  return (block_t *)((uint32_t)(block + 1) + size(block));
}
static inline uint8_t is_free(block_t *block)
{
  return block->next_free || block->prev_free || head == block;
}

// push_front and remove are the standard operations on the free list.
static void push_front(block_t *block)
{
  block->prev_free = NULL;
  block->next_free = head;
  if (head)
    head->prev_free = block;
  head = block;
}
static void remove(block_t *block)
{
  if (block->next_free)
    block->next_free->prev_free = block->prev_free;
  if (block->prev_free)
    block->prev_free->next_free = block->next_free;
  else
    head = block->next_free;

  block->next_free = NULL;
  block->prev_free = NULL;
}

// Split a block into two adjacent blocks.
// The new block header is placed at `(uint32_t)(block + 1) + split_offset`
// and is pushed onto the front of the free list.
static void split(block_t *block, size_t split_offset)
{
  block_t *new_block = (block_t *)((uint32_t)(block + 1) + split_offset);
  new_block->size_next = (size(block) - (split_offset + sizeof(block_t))) | (block->size_next & 1);
  new_block->prev = block;
  push_front(new_block);
  if (block->size_next & 1)
    next(block)->prev = new_block;
  block->size_next = split_offset | 1;
}

// Merge a block with the next block.
// This function does not check the `next` flag and assumes that the blocks
// are adjacent. The next block is removed from the free list.
static block_t *merge(block_t *block)
{
  block_t *next_block = next(block);
  remove(next_block);
  if (next_block->size_next & 1)
    next(next_block)->prev = block;
  block->size_next =
    (size(block) + size(next_block) + sizeof(block_t)) | (next_block->size_next & 1);
  return block;
}

// Allocate enough pages to make a block with the given size.
// The `size` parameter does not include the size of the block header.
// The new block is pushed onto the front of the free list.
static void alloc_pages(size_t size)
{
  size = u_page_align_up(size + sizeof(block_t));

  uint32_t npages = size >> PHYS_ADDR_OFFSET;
  uint32_t vaddr = paging_next_vaddr(npages, KERNEL_START_VADDR);
  if (vaddr == 0)
    return;

  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  uint32_t acquired_size = 0;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    if (paddr == 0)
      break;
    paging_result_t res = paging_map(vaddr + acquired_size, paddr, flags);
    if (res != PAGING_OK)
      break;
    acquired_size += PAGE_SIZE;
  }

  if (acquired_size == 0)
    return;

  block_t *new_block = (block_t *)vaddr;
  new_block->size_next = acquired_size - sizeof(block_t);
  new_block->prev = NULL;
  push_front(new_block);
}

// Unmap and free any pages that are within `block`.
static void free_pages(block_t *block)
{
  uint32_t block_start = (uint32_t)block;
  uint32_t block_end = (uint32_t)next(block);
  uint32_t page_start = u_page_align_up(block_start);
  uint32_t page_end = u_page_align_down(block_end);

  if (page_start > block_start && page_start - block_start < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_start += PAGE_SIZE;
  if (block_end > page_end && block_end - page_end < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_end -= PAGE_SIZE;

  if (page_start >= page_end || page_end - page_start < PAGE_FREE_THRESHOLD)
    return;

  block_t *removed_block = block;
  if (page_start > block_start) {
    split(removed_block, page_start - (uint32_t)(removed_block + 1));
    removed_block = head;
  }
  if (block_end > page_end)
    split(removed_block, page_end - (uint32_t)(removed_block + 1));

  if (removed_block->size_next & 1)
    next(removed_block)->prev = NULL;
  if (removed_block->prev)
    removed_block->prev->size_next &= ~1;
  remove(removed_block);

  for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
    pmm_free(paging_get_paddr(vaddr), 1);
    paging_unmap(vaddr);
  }
}

void *kmalloc(size_t sz)
{
  if (sz == 0)
    return NULL;
  if (sz < MIN_BLOCK_SIZE)
    sz = MIN_BLOCK_SIZE;
  if (sz & 1)
    ++sz;

  // TODO: maybe use a klock instead of disabling interrupts?
  // The overhead of getting pre-empted while mallocing and blocking
  // other tasks from accessing the heap may outweigh the cost of disabling
  // scheduler interrupts.
  uint32_t eflags = interrupt_save_disable();

  block_t *current = head;
  while (current && size(current) < sz)
    current = current->next_free;

  if (current == NULL) {
    alloc_pages(sz);
    current = head;
  }
  if (current == NULL || size(current) < sz) {
    interrupt_restore(eflags);
    return NULL;
  }

  if (size(current) - sz >= sizeof(block_t) + MIN_BLOCK_SIZE)
    split(current, sz);

  remove(current);

  interrupt_restore(eflags);

  return (void *)(current + 1);
}

void kfree(void *ptr)
{
  if (ptr == NULL)
    return;

  uint32_t eflags = interrupt_save_disable();

  block_t *block = (block_t *)ptr - 1;
  if (is_free(block)) {
    interrupt_restore(eflags);
    return;
  }

  push_front(block);

  if (block->prev && is_free(block->prev)) {
    block = block->prev;
    merge(block);
  }
  if ((block->size_next & 1) && is_free(next(block)))
    merge(block);

  if (size(block) + sizeof(block_t) >= PAGE_FREE_THRESHOLD)
    free_pages(block);

  interrupt_restore(eflags);
}
