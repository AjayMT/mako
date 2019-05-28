
// pmm.c
//
// Physical memory manager for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <common/constants.h>
#include <common/multiboot.h>
#include <debug/log.h>
#include <util/util.h>
#include "pmm.h"

#define MAX_MEMORY_MAP_ENTRIES 100
#define BITMAP_ARRAY_SIZE      0x8000

// A single memory map entry.
// `addr` is the physical start address, `len` is the size
// of the region in bytes.
typedef struct memory_map_entry_s {
  uint32_t addr;
  uint32_t len;
} memory_map_entry_t;

// A memory map with up to 100 entries.
// Only regions marked 'available' are in the map.
typedef struct memory_map_s {
  memory_map_entry_t entries[MAX_MEMORY_MAP_ENTRIES];
  uint32_t size;
} memory_map_t;

// Global state.
static memory_map_t pmm_mmap;
static uint32_t free_page_bitmap[BITMAP_ARRAY_SIZE];
static uint32_t free_page_count = 0;

// Page alignment functions.
static inline uint32_t page_align_up(uint32_t addr)
{
  if (addr != (addr & 0xFFFFF000))
    addr = (addr & 0xFFFFF000) + PAGE_SIZE;
  return addr;
}
static inline uint32_t page_align_down(uint32_t addr)
{ return addr & 0xFFFFF000; }

// Mark a physical page frame as free.
static void mark_page_free(uint32_t page_number)
{
  uint32_t index = page_number >> 5; // Divide by 32.
  uint32_t bit = page_number & 0b11111; // Mod 32.
  if (index >= BITMAP_ARRAY_SIZE) return;
  if ((free_page_bitmap[index] & (1 << bit)) == 0)
    ++free_page_count; // Page was not free.
  free_page_bitmap[index] |= (1 << bit);
}

// Mark a physical page frame as used.
static void mark_page_used(uint32_t page_number)
{
  uint32_t index = page_number >> 5; // Divide by 32.
  uint32_t bit = page_number & 0b11111; // Mod 32.
  if (index >= BITMAP_ARRAY_SIZE) return;
  if (free_page_bitmap[index] & (1 << bit))
    --free_page_count; // Page was free.
  free_page_bitmap[index] &= ~(1 << bit);
}

// Mark free page frames from a bitmap.
static void bitmap_init(memory_map_t mmap)
{
  for (uint32_t i = 0; i < mmap.size; ++i) {
    memory_map_entry_t entry = mmap.entries[i];
    uint32_t start_addr = page_align_up(entry.addr);
    uint32_t end_addr = page_align_down(entry.addr + entry.len);
    uint32_t page_number = start_addr >> PHYS_ADDR_OFFSET;
    uint32_t end_number = end_addr >> PHYS_ADDR_OFFSET;
    for (; page_number < end_number; ++page_number)
      mark_page_free(page_number);
  }
}

// Retrieve the memory map from GRUB multiboot info.
static memory_map_t get_mmap(
  multiboot_info_t *mb_info,
  const uint32_t kphys_start,
  const uint32_t kphys_end
  )
{
  memory_map_t mmap;
  u_memset(&mmap, 0, sizeof(mmap));
  multiboot_memory_map_t *entry;

  for (
    entry = (multiboot_memory_map_t *)mb_info->mmap_addr;
    (uint32_t)entry < mb_info->mmap_addr + mb_info->mmap_length
      && mmap.size < MAX_MEMORY_MAP_ENTRIES;
    entry = (multiboot_memory_map_t *)
      ((uint32_t)entry + entry->size + sizeof(entry->size))
    )
  {
    if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
      continue;

    uint32_t addr = entry->addr;
    uint32_t len = entry->len;

    if (addr <= kphys_start && addr + len > kphys_end) {
      // All of the memory occupied by the kernel must be 'available'
      // so the only way an mmap entry would overlap with the kernel
      // is if the start address is lte the start of the kernel
      // and the end address goes past the end of the kernel.
      // In this case we just move the region past the kernel.
      len -= kphys_end - addr;
      addr = kphys_end;
    }

    // If the address is below 1MB, exclude it.
    // GRUB's mmap doesn't include some stuff that's mapped to memory
    // below 1MB.
    if (addr <= 0x100000) continue;

    mmap.entries[mmap.size].addr = addr;
    mmap.entries[mmap.size].len = len;
    ++mmap.size;
  }

  return mmap;
}

// Initialize the physical memory manager.
uint32_t pmm_init(
  multiboot_info_t *mb_info,
  const uint32_t kphys_start,
  const uint32_t kphys_end
  )
{
  u_memset(free_page_bitmap, 0, sizeof(free_page_bitmap));

  if ((mb_info->flags & 0x20) == 0) {
    log_error("pmm", "No memory map from GRUB.\n");
    return 1;
  }

  pmm_mmap = get_mmap(mb_info, kphys_start, kphys_end);
  if (pmm_mmap.size == 0) {
    log_error("pmm", "Could not locate any available memory.\n");
    return 1;
  }

  bitmap_init(pmm_mmap);
  log_info("pmm", "Found %u free pages.\n", free_page_count);

  return 0;
}

// Allocate multiple contiguous physical pages.
uint32_t pmm_alloc(uint32_t size)
{
  if (free_page_count == 0 || size == 0) return 0;

  uint32_t max_page_number = sizeof(free_page_bitmap);
  uint32_t current = 0, step = 0;
  for (; current < max_page_number; current += step + 1) {
    uint32_t current_index = current >> 5;
    if (free_page_bitmap[current_index] == 0)
      current = (current_index + 1) << 5;

    uint32_t found = 0;
    for (step = 0; step < size; ++step) {
      uint32_t page_number = current + step;
      uint32_t index = page_number >> 5;
      uint32_t bit = page_number & 0b11111;
      uint32_t value = free_page_bitmap[index];
      if (value & (1 << bit))
        ++found;
      if (found >= size) {
        for (uint32_t allocated = 0; allocated < size; ++allocated)
          mark_page_used(current + allocated);
        return current << PHYS_ADDR_OFFSET;
      }
      if ((value & (1 << bit)) == 0)
        break;
    }
  }

  return 0;
}

// Free multiple contiguous physical pages.
void pmm_free(uint32_t addr, uint32_t size)
{
  addr = page_align_down(addr);
  for (uint32_t i = 0; i < size; ++i, addr += PAGE_SIZE)
    mark_page_free(addr >> PHYS_ADDR_OFFSET);
}
