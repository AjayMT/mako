
// kheap.c
//
// Kernel heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <paging/paging.h>
#include <pmm/pmm.h>
#include <interrupt/interrupt.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include "kheap.h"

// This is a relatively simple worst-fit allocator. It maintains
// a list of free blocks of memory, always sorted from largest to
// smallest. Theoretically, the advantages of this are:
//   - Allocations are fast since we can always use the first block
//     in the list, unless more memory is needed from the kernel.
//   - External fragmentation is encouraged, but the 'holes' are
//     usually large enough to be useful.
// Theoretical performance benefits from the worst-fit ordering may
// be offset by the fact that blocks have to be re-ordered whenever
// they are shrunk or grown. Nevertheless, this was more interesting
// to implement than a simple first-fit allocator.

// Constants.
static const uint32_t SIZE_UNIT         = 8;
static const uint32_t SIZE_UNIT_OFFSET  = 3;
static const uint64_t BLOCK_MAGIC       = 0xDEADDEAD;

// The back of every block stores its size and three flags:
//   `free`: whether the block is free
//   `prev`: whether a block exists adjacent to this one in
//           memory, before it
//   `next`: whether a block exists adjacent to this one in
//           memory, after it.
// The size can be stored with only 29 bits because we force it
// to be a multiple of 8, so the last three bits are always zero.
struct block_back_s {
  uint32_t size : 29;
  uint32_t free : 1;
  uint32_t prev : 1;
  uint32_t next : 1;
} __attribute__((packed));
typedef struct block_back_s block_back_t;

// The front of every block stores pointers to the next (smaller) and
// previous (bigger) blocks in the size chain, and a pointer to the
// struct at the back of the block.
struct block_front_s {
  struct block_front_s *bigger;
  struct block_front_s *smaller;
  block_back_t *info;
  uint64_t magic;
} __attribute__((packed));
typedef struct block_front_s block_front_t;

// Minimum size of a single block, including front and back
// info structs.
static const uint32_t MIN_SIZE = sizeof(block_front_t)
  + sizeof(block_back_t) + SIZE_UNIT;

// The largest available block and head of the size list.
static block_front_t *biggest = NULL;

// Utility functions.
static inline uint32_t page_align_up(uint32_t addr)
{
  if (addr != (addr & 0xFFFFF000))
    addr = (addr & 0xFFFFF000) + PAGE_SIZE;
  return addr;
}
static inline uint32_t page_align_down(uint32_t addr)
{ return addr & 0xFFFFF000; }
static inline uint32_t block_align_up(uint32_t addr)
{
  if ((addr & 0b111) != 0)
    addr += SIZE_UNIT - (addr & 0b111);
  return addr;
}
static inline size_t get_size(block_front_t *block)
{ return (block->info->size) << SIZE_UNIT_OFFSET; }

// Remove a free block from the size list.
static void remove_block(block_front_t *block)
{
  if (block->bigger)
    block->bigger->smaller = block->smaller;
  else biggest = block->smaller;
  if (block->smaller)
    block->smaller->bigger = block->bigger;

  block->bigger = NULL;
  block->smaller = NULL;
}

// Move a block down the size list to its correct position.
static void sort_down(block_front_t *block)
{
  block_front_t *swap = block->smaller;
  block_front_t *prev = block;
  for (
    ;
    swap && get_size(block) < get_size(swap);
    prev = swap, swap = swap->smaller
    );

  if (prev == block) return;

  remove_block(block);
  block->smaller = swap;
  block->bigger = prev;
  if (block->smaller) block->smaller->bigger = block;
  if (block->bigger) block->bigger->smaller = block;
}

// Move a block up the size list to its correct position.
static void sort_up(block_front_t *block)
{
  block_front_t *swap = block->bigger;
  block_front_t *prev = block;
  for (
    ;
    swap && get_size(block) > get_size(swap);
    prev = swap, swap = swap->bigger
    );

  if (prev == block) return;

  remove_block(block);
  block->bigger = swap;
  block->smaller = prev;
  if (block->smaller) block->smaller->bigger = block;
  if (block->bigger) block->bigger->smaller = block;
  else biggest = block;
}

// Get the front of the previous block.
static inline block_front_t *previous_block(block_front_t *block)
{
  uint32_t pinfo_addr = (uint32_t)block - sizeof(block_back_t);
  uint32_t psize = ((block_back_t *)pinfo_addr)->size << SIZE_UNIT_OFFSET;
  uint32_t pfront_addr = pinfo_addr - psize - sizeof(block_front_t);
  return (block_front_t *)pfront_addr;
}

// Get the front of the next block.
static inline block_front_t *next_block(block_front_t *block)
{
  uint32_t info_addr = (uint32_t)(block->info);
  return (block_front_t *)(info_addr + sizeof(block_back_t));
}

// Split a block at an offset relative to the base of the
// free region within the block.
static block_front_t *split_block(block_front_t *block, size_t offset)
{
  if (offset >= get_size(block)) return NULL;

  uint32_t block_addr = (uint32_t)block;
  uint32_t new_front_addr = block_addr + sizeof(block_front_t) + offset;
  uint32_t new_info_addr = new_front_addr - sizeof(block_back_t);
  block_back_t old_info = *(block->info);

  block_front_t new_front = {
    .bigger = block,
    .smaller = block->smaller,
    .info = block->info,
    .magic = BLOCK_MAGIC
  };
  *((block_front_t *)new_front_addr) = new_front;
  new_front.info->size =
    (get_size(block) - offset - sizeof(block_front_t)) >> SIZE_UNIT_OFFSET;
  new_front.info->prev = 1;
  block->smaller = (block_front_t *)new_front_addr;
  if (new_front.smaller)
    new_front.smaller->bigger = (block_front_t *)new_front_addr;

  block_back_t new_info = {
    .size = (offset - sizeof(block_back_t)) >> SIZE_UNIT_OFFSET,
    .free = old_info.free,
    .prev = old_info.prev,
    .next = 1
  };
  *((block_back_t *)new_info_addr) = new_info;
  block->info = (block_back_t *)new_info_addr;

  return (block_front_t *)new_front_addr;
}

// Merge a block with the next block.
static void merge_block(block_front_t *block)
{
  if (block->info->next == 0) return;

  block_front_t *nb = next_block(block);
  remove_block(nb);

  block_back_t old_info = *(block->info);
  uint32_t block_size = get_size(block);
  uint32_t nb_size = get_size(nb);
  block_size += nb_size + sizeof(block_front_t) + sizeof(block_back_t);

  block->info = nb->info;
  block->info->size = block_size >> SIZE_UNIT_OFFSET;
  block->info->prev = old_info.prev;
  block->info->free = old_info.free;
}

// Get more memory from the physical allocator and map it.
static void get_heap(size_t size)
{
  size += sizeof(block_front_t) + sizeof(block_back_t);
  size = page_align_up(size);

  uint32_t npages = size >> PHYS_ADDR_OFFSET;
  uint32_t vaddr = paging_next_vaddr(npages, KERNEL_START_VADDR);
  if (vaddr == 0) return;
  uint32_t acquired_size = 0;
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    if (paddr == 0) break;
    paging_result_t res = paging_map(vaddr + acquired_size, paddr, flags);
    if (res != PAGING_OK) break;
    acquired_size = (i + 1) << PHYS_ADDR_OFFSET;
  }

  if (acquired_size < MIN_SIZE) return;

  size_t new_size = acquired_size
    - sizeof(block_front_t) - sizeof(block_back_t);
  block_back_t new_info = {
    .size = new_size >> SIZE_UNIT_OFFSET,
    .free = 1,
    .prev = 0,
    .next = 0
  };
  uint32_t new_info_addr = (vaddr + acquired_size - sizeof(block_back_t));
  *((block_back_t *)new_info_addr) = new_info;
  block_front_t new_front = {
    .bigger = NULL,
    .smaller = biggest,
    .info = (block_back_t *)new_info_addr,
    .magic = BLOCK_MAGIC
  };
  *((block_front_t *)vaddr) = new_front;
  biggest = (block_front_t *)vaddr;
  sort_down(biggest);
}

// Return memory to the physical allocator and unmap pages.
static void release_heap(block_front_t *block)
{
  uint32_t block_addr = (uint32_t)block;
  uint32_t back_addr = (uint32_t)(block->info);
  uint32_t page_base_addr = page_align_up(block_addr);
  uint32_t page_top_addr = page_align_down(back_addr + sizeof(block_back_t));
  uint32_t front_space = page_base_addr - block_addr;
  uint32_t back_space = back_addr + sizeof(block_back_t) - page_top_addr;

  if (page_base_addr >= page_top_addr) return;

  block_front_t *left_block = block;
  block_front_t *page_block = NULL;
  block_front_t *right_block = NULL;

  if (front_space == 0) {
    left_block = NULL;
    page_block = block;
  } else if (front_space < MIN_SIZE) {
    block_front_t *pb = previous_block(block);
    if (pb->info->free == 0) {
      // Temporarily place an allocated block in the free list.
      pb->smaller = block->smaller;
      pb->bigger = block->bigger;
    }
    merge_block(pb);
    left_block = pb;
    block_addr = (uint32_t)left_block;
    front_space = page_base_addr - block_addr;
  }

  if (page_block == NULL)
    page_block = split_block(
      left_block, front_space - sizeof(block_front_t)
      );

  if (back_space >= MIN_SIZE)
    right_block = split_block(
      page_block, page_top_addr - page_base_addr - sizeof(block_front_t)
      );
  else {} // TODO Shift the front of the next block back?

  remove_block(page_block);
  if (left_block) left_block->info->next = 0;
  if (right_block) right_block->info->prev = 0;
  if (left_block && left_block->info->free == 0)
    remove_block(left_block);

  for (
    uint32_t vaddr = page_base_addr;
    vaddr < page_top_addr;
    vaddr += PAGE_SIZE
    )
  {
    pmm_free(paging_get_paddr(vaddr), 1);
    paging_unmap(vaddr);
  }

  if (right_block) sort_down(right_block);
  if (left_block) sort_down(left_block);
}

// Allocate memory.
void *kmalloc(size_t size)
{
  uint32_t flags = interrupt_save_disable();

  size = block_align_up(size);
  if (size == 0) return NULL;
  if (biggest == NULL || get_size(biggest) < size) {
    get_heap(size);
    if (biggest == NULL || get_size(biggest) < size)
      return NULL;
  }

  if (get_size(biggest) - size < MIN_SIZE)
    size = get_size(biggest);
  else split_block(biggest, size + sizeof(block_back_t));

  block_front_t *ret = biggest;
  remove_block(biggest); // Updates biggest.
  ret->info->free = 0;

  if (biggest) sort_down(biggest);

  interrupt_restore(flags);
  return (void *)((uint32_t)ret + sizeof(block_front_t));
}

// Free memory.
void kfree(void *ptr)
{
  if (ptr == NULL) return;
  if (*((uint32_t *)((uint32_t)ptr - sizeof(uint32_t))) != BLOCK_MAGIC)
    return;

  uint32_t flags = interrupt_save_disable();

  block_front_t *block = (block_front_t*)
    ((uint32_t)ptr - sizeof(block_front_t));
  if (block->info->free) return;
  block->info->free = 1;

  if (block->info->prev) {
    block_front_t *pb = previous_block(block);
    if (pb->info->free) {
      merge_block(pb);
      block = pb;
    }
  }

  if (block->info->next) {
    block_front_t *nb = next_block(block);
    if (nb->info->free) merge_block(block);
  }

  if (block->bigger == NULL && block->smaller == NULL) {
    block->smaller = biggest;
    biggest = block;
    sort_down(block);
  } else sort_up(block);

  release_heap(block);
  interrupt_restore(flags);
}
