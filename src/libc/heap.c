
// heap.c
//
// User heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "stdint.h"
#include "mako.h"
#include "stdlib.h"
#include "string.h"

// This is a simple first-fit allocator with block splitting / coalescing.

static const size_t MIN_BLOCK_SIZE = 8;
static const uint32_t PHYS_ADDR_OFFSET = 12;
static const uint32_t PAGE_SIZE = 0x1000;
static const uint32_t PAGE_FREE_THRESHOLD = PAGE_SIZE;

struct block_s {
  // The size of each block is always an even number and the last bit
  // is a flag that indicates whether an adjacent next block exists.
  uint32_t size_next;
  struct block_s *prev;
  struct block_s *prev_free;
  struct block_s *next_free;
} __attribute__((packed));
typedef struct block_s block_t;

static block_t *head = NULL;
static volatile uint32_t heap_lock = 0;

// Simple block properties.
static inline uint32_t size(block_t *block) { return block->size_next & (~1); }
static inline block_t *next(block_t *block) { return (block_t *)((uint32_t)(block + 1) + size(block)); }
static inline uint8_t is_free(block_t *block) { return block->next_free || block->prev_free; }

// Utility functions.
static inline size_t page_align_up(size_t a)
{
  if (a != (a & 0xFFFFF000))
    a = (a & 0xFFFFF000) + PAGE_SIZE;
  return a;
}
static inline size_t page_align_down(uint32_t a)
{ return a & 0xFFFFF000; }

// push_front and remove are the standard operations on the free list.
static void push_front(block_t *block)
{
  block->prev_free = NULL;
  block->next_free = head;
  if (head) head->prev_free = block;
  head = block;
}
static void remove(block_t *block)
{
  if (block->next_free) block->next_free->prev_free = block->prev_free;
  if (block->prev_free) block->prev_free->next_free = block->next_free;
  else head = block->next_free;

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
  if (block->size_next & 1) next(block)->prev = new_block;
  block->size_next = split_offset | 1;
}

// Merge a block with the next block.
// This function does not check the `next` flag and assumes that the blocks
// are adjacent. The next block is removed from the free list.
static block_t *merge(block_t *block)
{
  block_t *next_block = next(block);
  remove(next_block);
  if (next_block->size_next & 1) next(next_block)->prev = block;
  block->size_next = (size(block) + size(next_block) + sizeof(block_t)) | (next_block->size_next & 1);
  return block;
}

// Allocate enough pages to make a block with the given size.
// The `size` parameter does not include the size of the block header.
// The new block is pushed onto the front of the free list.
static void alloc_pages(size_t size)
{
  size = page_align_up(size + sizeof(block_t));
  uint32_t npages = size >> PHYS_ADDR_OFFSET;
  uint32_t vaddr = pagealloc(npages);
  if (vaddr == 0) return;

  block_t *new_block = (block_t *)vaddr;
  new_block->size_next = size - sizeof(block_t);
  new_block->prev = NULL;
  push_front(new_block);
}

// Unmap and free any pages that are within `block`.
static void free_pages(block_t *block)
{
  uint32_t block_start = (uint32_t)block;
  uint32_t block_end = (uint32_t)next(block);
  uint32_t page_start = page_align_up(block_start);
  uint32_t page_end = page_align_down(block_end);

  if (page_start > block_start && page_start - block_start < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_start += PAGE_SIZE;
  if (block_end > page_end && block_end - page_end < sizeof(block_t) + MIN_BLOCK_SIZE)
    page_end -= PAGE_SIZE;

  if (page_start >= page_end || page_end - page_start < PAGE_FREE_THRESHOLD) return;

  block_t *removed_block = block;
  if (page_start > block_start) {
    split(removed_block, page_start - (uint32_t)(removed_block + 1));
    removed_block = head;
  }
  if (block_end > page_end)
    split(removed_block, page_end - (uint32_t)(removed_block + 1));

  if (removed_block->size_next & 1) next(removed_block)->prev = NULL;
  if (removed_block->prev) removed_block->prev->size_next &= ~1;
  remove(removed_block);

  uint32_t npages = (page_end - page_start) >> PHYS_ADDR_OFFSET;
  pagefree(page_start, npages);
}

void *malloc(size_t sz)
{
  if (sz == 0) return NULL;
  if (sz < MIN_BLOCK_SIZE) sz = MIN_BLOCK_SIZE;
  if (sz & 1) ++sz;

  thread_lock(&heap_lock);

  block_t *current = head;
  while (current && size(current) < sz)
    current = current->next_free;

  if (current == NULL) { alloc_pages(sz); current = head; }
  if (current == NULL || size(current) < sz) {
    thread_unlock(&heap_lock);
    return NULL;
  }

  if (size(current) - sz >= sizeof(block_t) + MIN_BLOCK_SIZE)
    split(current, sz);

  remove(current);

  thread_unlock(&heap_lock);

  return (void *)(current + 1);
}

void free(void *ptr)
{
  if (ptr == NULL) return;

  thread_lock(&heap_lock);

  block_t *block = (block_t *)ptr - 1;
  if (!is_free(block)) { thread_unlock(&heap_lock); return; }

  push_front(block);

  if (block->prev && is_free(block->prev)) {
    block = block->prev;
    merge(block);
  }
  if ((block->size_next & 1) && is_free(next(block)))
    merge(block);

  if (size(block) + sizeof(block_t) >= PAGE_FREE_THRESHOLD)
    free_pages(block);

  thread_unlock(&heap_lock);
}

void *realloc(void *ptr, size_t sz)
{
  if (ptr == NULL) return malloc(sz);
  if (sz < MIN_BLOCK_SIZE) sz = MIN_BLOCK_SIZE;
  if (sz & 1) ++sz;

  thread_lock(&heap_lock);

  block_t *block = (block_t *)ptr - 1;
  if (sz <= size(block)) { thread_unlock(&heap_lock); return ptr; }

  if ((block->size_next & 1) && is_free(next(block))) {
    size_t new_size = size(block) + size(next(block)) + sizeof(block_t);
    if (new_size >= sz) {
      merge(block);
      thread_unlock(&heap_lock);
      return ptr;
    }
  }

  thread_unlock(&heap_lock);

  char *p = malloc(sz);
  if (p == NULL) return NULL;
  memcpy(p, (char *)ptr, size(block));
  free(ptr);
  return p;
}
