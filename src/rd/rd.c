
// rd.c
//
// Ramdisk filesystem.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <common/constants.h>
#include <paging/paging.h>
#include <kheap/kheap.h>
#include <util/util.h>
#include <debug/log.h>
#include "rd.h"

#define RD_FILE_NAME_LEN   128
#define RD_NUM_DIR_ENTRIES 16

struct rd_dir_entry_s {
  char name[RD_FILE_NAME_LEN];
  uint32_t size;
  uint8_t is_dir;
} __attribute__((packed));
typedef struct rd_dir_entry_s rd_dir_entry_t;
typedef rd_dir_entry_t *rd_dir_header_t;

static rd_dir_header_t rd_root = NULL;
static uint32_t rd_size = 0;

static inline void map_fs_ops(fs_node_t *node)
{
  node->open = rd_open;
  node->close = rd_close;
  node->read = rd_read;
  node->write = rd_write;
  node->readdir = rd_readdir;
  node->finddir = rd_finddir;
}

// Initialize the ramdisk and mount it.
uint32_t rd_init(const uint32_t rd_phys_start, const uint32_t rd_phys_end)
{
  rd_size = rd_phys_end - rd_phys_start;
  uint32_t _rd_phys_start = rd_phys_start & 0xFFFFF000;
  uint32_t _rd_size = rd_phys_end - _rd_phys_start;
  if (_rd_size != (_rd_size & 0xFFFFF000))
    _rd_size = (_rd_size & 0xFFFFF000) + PAGE_SIZE;

  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(page_table_entry_t));

  uint32_t npages = _rd_size >> PHYS_ADDR_OFFSET;
  uint32_t base_vaddr = paging_next_vaddr(npages, KERNEL_START_VADDR);
  for (uint32_t i = 0; i < npages; ++i) {
    paging_result_t res = paging_map(
      base_vaddr + (i * PAGE_SIZE), _rd_phys_start + (i * PAGE_SIZE), flags
      );
    if (res != PAGING_OK) {
      log_error("rd", "Unable to map ramdisk to virtual memory.");
      return 1;
    }
  }

  rd_root = (rd_dir_header_t)(base_vaddr + (rd_phys_start - _rd_phys_start));

  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  u_memset(node, 0, sizeof(fs_node_t));
  node->flags = FS_DIRECTORY;
  map_fs_ops(node);

  fs_mount(node, "/rd");

  return 0;
}

void rd_open(fs_node_t *node, uint32_t flags)
{}
void rd_close(fs_node_t *node)
{}

uint32_t rd_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (offset >= node->length) return 0;
  if (offset + size >= node->length) size = node->length - offset;

  char *ptr = (char *)((uint32_t)rd_root + node->inode);
  u_memcpy(buffer, ptr + offset, size);
  return size;
}

uint32_t rd_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{ return 0; }

struct dirent *rd_readdir(fs_node_t *node, uint32_t index)
{
  struct dirent *ent = kmalloc(sizeof(struct dirent));
  if (index == 0) {
    u_memcpy(ent->name, FS_DIR_SELF, u_strlen(FS_DIR_SELF) + 1);
    ent->ino = node->inode;
    return ent;
  }

  // TODO ".."
  --index;
  rd_dir_header_t header = (rd_dir_header_t)((uint32_t)rd_root + node->inode);
  rd_dir_entry_t entry = header[index];
  if (entry.name[0] == 0) {
    kfree(ent);
    return NULL;
  }

  uint32_t size = RD_NUM_DIR_ENTRIES * sizeof(rd_dir_entry_t);
  for (uint32_t i = 0; i < index; ++i)
    size += header[i].size;

  u_memcpy(ent->name, entry.name, u_strlen(entry.name) + 1);
  ent->ino = node->inode + size;

  return ent;
}

fs_node_t *rd_finddir(fs_node_t *node, char *name)
{
  rd_dir_header_t header = (rd_dir_header_t)((uint32_t)rd_root + node->inode);
  uint32_t size = RD_NUM_DIR_ENTRIES * sizeof(rd_dir_entry_t);
  for (uint32_t i = 0; i < RD_NUM_DIR_ENTRIES; ++i) {
    rd_dir_entry_t ent = header[i];
    if (ent.name[0] == 0) break;
    if (u_strcmp(ent.name, name) != 0) {
      size += ent.size;
      continue;
    }

    fs_node_t *new_node = kmalloc(sizeof(fs_node_t));
    u_memset(new_node, 0, sizeof(fs_node_t));
    u_memcpy(new_node->name, name, u_strlen(name) + 1);
    new_node->flags = ent.is_dir ? FS_DIRECTORY : FS_FILE;
    new_node->inode = node->inode + size;
    new_node->length = ent.size;
    map_fs_ops(new_node);

    return new_node;
  }

  return NULL;
}
