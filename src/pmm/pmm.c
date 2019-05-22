
// pmm.c
//
// Physical memory manager for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <common/multiboot.h>
#include <debug/log.h>
#include "pmm.h"

#define MAX_MEMORY_MAP_ENTRIES 100

// A single memory map entry.
// `addr` is the physical start address, `len` is the size
// of the region.
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

static memory_map_t pmm_mmap;

// Retrieve the memory map from GRUB multiboot info.
static memory_map_t get_mmap(
  multiboot_info_t *mb_info,
  const uint32_t kphys_start,
  const uint32_t kphys_end
  )
{
  memory_map_t mmap; mmap.size = 0;
  multiboot_memory_map_t *entry;

  for (
    entry = (multiboot_memory_map_t *)mb_info->mmap_addr;

    (uint32_t)entry < mb_info->mmap_addr + mb_info->mmap_length
      && mmap.size <= MAX_MEMORY_MAP_ENTRIES;

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
  if ((mb_info->flags & 0x20) == 0) { // No memory map from GRUB.
    log_error("pmm", "No memory map from GRUB.");
    return 1;
  }

  pmm_mmap = get_mmap(mb_info, kphys_start, kphys_end);
  if (pmm_mmap.size == 0) {
    log_error("pmm", "Could not locate any available memory.");
    return 1;
  }

  return 0;
}
