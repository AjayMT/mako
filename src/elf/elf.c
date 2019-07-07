
// elf.c
//
// ELF binary loader.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <process/process.h>
#include <kheap/kheap.h>
#include <common/errno.h>
#include <util/util.h>
#include <debug/log.h>
#include "elf.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("elf", msg "\n"); return (code);  \
  }

uint8_t elf_is_valid(uint8_t *buf)
{
  return buf[0] == ELFMAG0
    && buf[1] == ELFMAG1
    && buf[2] == ELFMAG2
    && buf[3] == ELFMAG3;
}

uint8_t elf_load(process_image_t *img, uint8_t *buf)
{
  u_memset(img, 0, sizeof(process_image_t));
  CHECK(elf_is_valid(buf) == 0, "Not a valid ELF executable.", 1);

  Elf32_Header *ehdr = (Elf32_Header *)buf;

  // Not sure what to do with non-executable files.
  CHECK(ehdr->e_type != ET_EXEC, "Cannot load non-executable file.", 1);

  // We are assuming that files only have two segments: text and data.
  // This is not a safe assumption to make, but ehh...
  CHECK(ehdr->e_phnum > 2, "Too many segments in executable file.", 1);

  img->entry = ehdr->e_entry;
  for (uint32_t idx = 0; idx < ehdr->e_phnum; idx++) {
    Elf32_Phdr *phdr = (Elf32_Phdr *)(buf + ehdr->e_phoff
                                      + (idx * ehdr->e_phentsize));
    if (phdr->p_type != PT_LOAD) continue;

    if (phdr->p_flags & PF_X) { // Text section.
      img->text_vaddr = phdr->p_vaddr;
      img->text_len = phdr->p_memsz;
      img->text = kmalloc(phdr->p_memsz);
      CHECK(img->text == NULL, "No memory.", ENOMEM);
      u_memcpy(img->text, buf + phdr->p_offset, phdr->p_filesz);
      if (phdr->p_memsz > phdr->p_filesz)
        u_memset(img->text + phdr->p_memsz, 0, phdr->p_filesz);
      continue;
    }

    // Data section.
    img->data_vaddr = phdr->p_vaddr;
    img->data_len = phdr->p_memsz;
    img->data = kmalloc(phdr->p_memsz);
    CHECK(img->data == NULL, "No memory.", ENOMEM);
    u_memcpy(img->data, buf + phdr->p_offset, phdr->p_filesz);
    if (phdr->p_memsz > phdr->p_filesz)
      u_memset(img->data + phdr->p_memsz, 0, phdr->p_filesz);
  }

  return 0;
}
