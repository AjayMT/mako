
// heap.c
//
// User heap.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <mako.h>
#include <string.h>
#include <stdlib.h>


// This is adapted from the kernel's heap implementation, see `src/kheap`.

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
static const uint32_t PHYS_ADDR_OFFSET = 12;
static const uint32_t PAGE_SIZE        = 0x1000;
static const uint32_t BLOCK_MAGIC      = 0xDEADDEAD;

// The back of every block stores its size and three flags:
//   `free`: whether the block is free
//   `prev`: whether a block exists adjacent to this one in
//           memory, before it
//   `next`: whether a block exists adjacent to this one in
//           memory, after it.
struct block_back_s {
  uint32_t size   : 32;
  uint8_t free    : 1;
  uint8_t prev    : 1;
  uint8_t next    : 1;
  uint8_t padding : 5;
} __attribute__((packed));
typedef struct block_back_s block_back_t;

// The front of every block stores pointers to the next (smaller) and
// previous (bigger) blocks in the size chain, and a pointer to the
// struct at the back of the block. It also stores a magic number to
// confirm that it is a malloced block.
struct block_front_s {
  struct block_front_s *bigger;
  struct block_front_s *smaller;
  block_back_t *info;
  uint32_t magic;
} __attribute__((packed));
typedef struct block_front_s block_front_t;

// Minimum size of a single block, including front and back
// info structs.
static const uint32_t MIN_SIZE = sizeof(block_front_t)
  + sizeof(block_back_t) + 1;

// The largest available block and head of the size list.
static block_front_t *biggest = NULL;

// Lock for synchronisation.
static volatile uint32_t heap_lock = 0;

// Utility functions.
static inline uint32_t page_align_up(uint32_t addr)
{
  if (addr != (addr & 0xFFFFF000))
    addr = (addr & 0xFFFFF000) + PAGE_SIZE;
  return addr;
}
static inline uint32_t page_align_down(uint32_t addr)
{ return addr & 0xFFFFF000; }

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
    swap && block->info->size < swap->info->size;
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
    swap && block->info->size > swap->info->size;
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
  uint32_t psize = ((block_back_t *)pinfo_addr)->size;
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
  if (offset >= block->info->size) return NULL;

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
  new_front.info->size = block->info->size - offset - sizeof(block_front_t);
  new_front.info->prev = 1;
  block->smaller = (block_front_t *)new_front_addr;
  if (new_front.smaller)
    new_front.smaller->bigger = (block_front_t *)new_front_addr;
  *((block_front_t *)new_front_addr) = new_front;

  block_back_t new_info = {
    .size = offset - sizeof(block_back_t),
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
  uint32_t block_size = block->info->size;
  uint32_t nb_size = nb->info->size;
  block_size += nb_size + sizeof(block_front_t) + sizeof(block_back_t);

  block->info = nb->info;
  block->info->size = block_size;
  block->info->prev = old_info.prev;
  block->info->free = old_info.free;
  memset(nb, 0, sizeof(block_front_t));
}

// Get more memory from the physical allocator and map it.
static void get_heap(size_t size)
{
  size += sizeof(block_front_t) + sizeof(block_back_t);
  size = page_align_up(size);

  uint32_t npages = size >> PHYS_ADDR_OFFSET;
  uint32_t vaddr = pagealloc(npages);
  if (vaddr == 0) return;

  block_back_t new_info = {
    .size = size - sizeof(block_front_t) - sizeof(block_back_t),
    .free = 1,
    .prev = 0,
    .next = 0
  };
  uint32_t new_info_addr = (vaddr + size - sizeof(block_back_t));
  *((block_back_t *)new_info_addr) = new_info;
  block_front_t new_front = {
    .bigger = NULL,
    .smaller = biggest,
    .info = (block_back_t *)new_info_addr,
    .magic = BLOCK_MAGIC
  };
  *((block_front_t *)vaddr) = new_front;
  if (biggest) biggest->bigger = (block_front_t *)vaddr;
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

  pagefree(page_base_addr, (page_top_addr - page_base_addr) / PAGE_SIZE);

  if (right_block) sort_down(right_block);
  if (left_block) sort_down(left_block);
}

// Allocate memory.
void *malloc(size_t size)
{
  if (size == 0) return NULL;

  thread_lock(&heap_lock);

  if (biggest == NULL || biggest->info->size < size) {
    get_heap(size);
    if (biggest == NULL || biggest->info->size < size) {
      thread_unlock(&heap_lock);
      return NULL;
    }
  }

  if (biggest->info->size - size < MIN_SIZE)
    size = biggest->info->size;
  else split_block(biggest, size + sizeof(block_back_t));

  block_front_t *ret = biggest;
  remove_block(biggest); // Updates biggest.
  ret->info->free = 0;

  if (biggest) sort_down(biggest);

  thread_unlock(&heap_lock);
  return (void *)((uint32_t)ret + sizeof(block_front_t));
}

// Free memory.
void free(void *ptr)
{
  if (ptr == NULL) return;
  if (*((uint32_t *)((uint32_t)ptr - sizeof(uint32_t))) != BLOCK_MAGIC)
    return;

  thread_lock(&heap_lock);

  block_front_t *block = (block_front_t*)
    ((uint32_t)ptr - sizeof(block_front_t));
  if (block->info->free) { thread_unlock(&heap_lock); return; }
  block->info->free = 1;
  uint8_t in_list = 0;

  if (block->info->prev) {
    block_front_t *pb = previous_block(block);
    if (pb->info->free) {
      merge_block(pb);
      block = pb;
      in_list = 1;
    }
  }

  if (block->info->next) {
    block_front_t *nb = next_block(block);
    if (nb->info->free) merge_block(block);
  }

  if (!in_list) {
    block->smaller = biggest;
    if (biggest) biggest->bigger = block;
    biggest = block;
    sort_down(block);
  } else sort_up(block);

  release_heap(block);
  thread_unlock(&heap_lock);
}

// Resize or create an allocation.
void *realloc(void *ptr, size_t size)
{
  if (ptr == NULL) return malloc(size);
  if (*((uint32_t *)((uint32_t)ptr - sizeof(uint32_t))) != BLOCK_MAGIC)
    return malloc(size);
  if (size == 0) ++size;

  thread_lock(&heap_lock);

  block_front_t *block = (block_front_t *)((uint32_t)ptr - sizeof(block_front_t));

  if (block->info->next && size > block->info->size) {
    block_front_t *nb = next_block(block);
    uint32_t nsize = block->info->size + nb->info->size
      + sizeof(block_front_t) + sizeof(block_back_t);
    if (nb->info->free && nsize >= size) {
      merge_block(block);
      thread_unlock(&heap_lock);
      return ptr;
    }
  }

  thread_unlock(&heap_lock);

  char *p = malloc(size);
  if (p == NULL) return NULL;

  if (size > block->info->size) size = block->info->size;
  memcpy(p, (char *)ptr, size);
  free(ptr);

  return p;
}
