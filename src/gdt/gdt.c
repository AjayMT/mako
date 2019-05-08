
// gdt.c
//
// Global descriptor table interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <common/stdint.h>
#include "gdt.h"

// x86 processors divide memory into lots of small regions a.k.a 'segments'.
// The Global Descriptor Table (GDT) describes the characteristics of these
// segments, such as
//   1. The base address of the segment.
//   2. The "limit" or size of the segment.
//   3. Access flags (readable/writable/executable)
//   4. Privilege level (user code can't touch kernel memory)
// and other stuff. Unfortunately the GDT is stored in some garbage
// backwards-compatible format in which the limit and base are fragmented.
// Requires huge balls and lots of bit-shifting to operate.

// GDT Entry struct.
struct gdt_entry_s {
  uint16_t limit_1;  // Lower 16 bits of the limit.
  uint16_t base_1;   // Lower 16 bits of the base.
  uint8_t base_2;    // Next 8 bits of the base.
  uint8_t access;    // Access flags.
  uint8_t limit_2;   // 4 highest bits of limit and some other stuff.
  uint8_t base_3;    // Last 8 bits of the base,
} __attribute__((packed));
typedef struct gdt_entry_s gdt_entry_t;

// Struct into which the LGDT instruction loads the GDT.
struct gdt_ptr_s {
  uint16_t limit;  // The size of the GDT.
  uint32_t base;   // The base address of the GDT.
} __attribute__((packed));
typedef struct gdt_ptr_s gdt_ptr_t;

// Constants.
static const uint16_t SEGMENT_BASE  = 0;
static const uint32_t SEGMENT_LIMIT = 0xFFFFF;
static const uint8_t CODE_RX_TYPE   = 0xA;
static const uint8_t DATA_RW_TYPE   = 0x2;

// The actual global descriptor table. 6 entries.
#define GDT_NUM_ENTRIES 6
static gdt_entry_t gdt_entries[GDT_NUM_ENTRIES];

// Load the global descriptor table. Implemented in gdt.s.
void gdt_load(uint32_t);

// Create an entry in the table.
static void gdt_create_entry(
  uint32_t index, uint8_t privilege_level, uint8_t type
  )
{
  gdt_entries[index].base_1  = SEGMENT_BASE & 0xFFFF;
  gdt_entries[index].base_2  = (SEGMENT_BASE >> 16) & 0xFF;
  gdt_entries[index].base_3  = (SEGMENT_BASE >> 24) & 0xFF;
  gdt_entries[index].limit_1 = (SEGMENT_LIMIT & 0xFFFF);

  // 4kB segments, 4 highest bits of limit, other things.
  gdt_entries[index].limit_2 |= (0x01 << 7) | (0x01 << 6) | 0x0F;

  // set type and privilege level
  gdt_entries[index].access |=
    (0x01 << 7)
    | ((privilege_level & 0x03) << 5)
    | (0x01 << 4)
    | (type & 0x0F);
}

// Initialize the GDT.
void gdt_init()
{
  gdt_ptr_t table_ptr;
  table_ptr.limit = sizeof(gdt_entry_t) * GDT_NUM_ENTRIES;
  table_ptr.base = (uint32_t)&gdt_entries;

  // Null entry. This is necessary.
  gdt_create_entry(0, 0, 0);

  // Kernel mode code segment.
  gdt_create_entry(1, PL0, CODE_RX_TYPE);

  // Kernel mode data segment.
  gdt_create_entry(2, PL0, DATA_RW_TYPE);

  // User mode code segment.
  gdt_create_entry(3, PL3, CODE_RX_TYPE);

  // User mode data segment.
  gdt_create_entry(4, PL3, DATA_RW_TYPE);

  // Execute LGDT instruction.
  gdt_load((uint32_t)&table_ptr);
}
