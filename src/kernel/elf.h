
// elf.h
//
// ELF binary loader.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _ELF_H_
#define _ELF_H_

#include "../common/stdint.h"
#include "process.h"

// ELF magic numbers.
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define EI_NIDENT 16

// ELF data types.
typedef uint32_t Elf32_Word;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint32_t Elf32_Sword;
typedef uint16_t Elf32_Half;

// ELF header.
typedef struct {
  uint8_t    e_ident[EI_NIDENT];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off  e_phoff;
  Elf32_Off  e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
} Elf32_Header;

// e_type values.
#define ET_NONE   0
#define ET_REL    1
#define ET_EXEC   2
#define ET_DYN    3
#define ET_CORE   4
#define ET_LOPROC 0xff0
#define ET_HIPROC 0xfff


// Program header.
typedef struct {
  Elf32_Word p_type;
  Elf32_Off  p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
} Elf32_Phdr;

// p_type values.
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_LOPROC  0x70000000
#define PT_HIPROC  0x7FFFFFFF

// p_flags values.
#define PF_X 1
#define PF_W 2
#define PF_R 4

uint8_t elf_is_valid(uint8_t *);
uint8_t elf_load(process_image_t *, uint8_t *);

#endif /* _ELF_H_ */
