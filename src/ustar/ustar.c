
// ustar.c
//
// USTAR filesystem implementation.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <kheap/kheap.h>
#include <klock/klock.h>
#include <util/util.h>
#include <common/errno.h>
#include <debug/log.h>
#include <fs/fs.h>
#include "ustar.h"

#define CHECK(err, msg, code) if ((err)) {          \
    log_error("ustar", msg "\n"); return (code);    \
  }
#define CHECK_UNLOCK(err, msg, code) if ((err)) {           \
    log_error("ustar", msg "\n"); kunlock(&(self->lock));   \
    return (code);                                          \
  }

typedef struct {
  fs_node_t *block_device;
  volatile uint32_t lock;
} ustar_fs_t;

struct ustar_metadata_s {
  char name[100];
  char mode[8];
  char uid[8];
  char gid[8];
  char size[12];
  char mtime[12];
  char checksum[8];
  char type;
  char linked_name[100];
  char ustar_magic[6];
  char version[2];
  char uname[32];
  char gname[32];
  char major[8];
  char minor[8];
  char prefix[155];
  char padding[12]; // to round size up to 512
} __attribute__((packed));
typedef struct ustar_metadata_s ustar_metadata_t;

#define NORMAL    0
#define NORMAL_  '0'
#define HARDLINK '1'
#define SYMLINK  '2'
#define CHARDEV  '3'
#define BLOCKDEV '4'
#define DIR      '5'
#define PIPE     '6'
#define FREE     '8'

#define USTAR_MAGIC "ustar"
#define BLOCK_SIZE 512

static inline uint32_t block_align_up(uint32_t n)
{
  if (n & (BLOCK_SIZE - 1))
    return n + BLOCK_SIZE - (n & (BLOCK_SIZE - 1));
  return n;
}

static uint32_t parse_oct(char *s, uint32_t size)
{
  uint32_t n = 0;
  for (uint32_t i = 0; i < size; ++i) {
    n <<= 3;
    n += s[i] - '0';
  }
  return n;
}

static void write_oct(char *out, uint32_t num, uint32_t size)
{
  for (int32_t i = size - 1; i >= 0; --i) {
    uint32_t n = num & 7;
    num >>= 3;
    out[i] = n + '0';
  }
}

static uint32_t ustar_find(ustar_fs_t *self, char *name)
{
  uint32_t disk_offset = 0;
  while (1) {
    ustar_metadata_t data;
    uint32_t read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );

    if (read_size != BLOCK_SIZE) return -1;
    if (u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0)
      return -1;

    if (u_strcmp(data.name, name) == 0) {
      if (data.type == FREE) return -1;
      return disk_offset;
    }

    uint32_t size = block_align_up(parse_oct(data.size, sizeof(data.size)));
    disk_offset += BLOCK_SIZE + size;
  }

  return -1;
}

static uint32_t ustar_alloc(ustar_fs_t *self, uint32_t size)
{
  // TODO
  return 0;
}

static void ustar_open(fs_node_t *node, uint32_t flags)
{
  if ((flags & O_TRUNC) == 0) return;
  // TODO truncate file
}

static void ustar_close(fs_node_t *node)
{}

static uint32_t ustar_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  ustar_fs_t *self = node->device;
  uint32_t disk_offset = ustar_find(self, node->name);
  CHECK(disk_offset == (uint32_t)-1, "File does not exist.", ENOENT);

  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);
  uint32_t file_size = parse_oct(data.size, sizeof(data.size));
  if (offset > file_size) return 0;
  if (offset + size > file_size) size = file_size - offset;
  return fs_read(self->block_device, disk_offset + BLOCK_SIZE + offset, size, buf);
}

static uint32_t ustar_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  ustar_fs_t *self = node->device;
  klock(&(self->lock));
  uint32_t disk_offset = ustar_find(self, node->name);
  CHECK_UNLOCK(disk_offset == (uint32_t)-1, "File does not exist.", ENOENT);

  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);
  uint32_t file_size = parse_oct(data.size, sizeof(data.size));
  uint32_t space = block_align_up(file_size) - file_size;

  uint32_t tmp_offset = disk_offset + BLOCK_SIZE + file_size + space;
  while (1) {
    ustar_metadata_t tmp_data;
    read_size = fs_read(
      self->block_device, tmp_offset, BLOCK_SIZE, (uint8_t *)&tmp_data
      );
    if (read_size != BLOCK_SIZE) break;
    if (u_strcmp(tmp_data.ustar_magic, USTAR_MAGIC) != 0) break;
    if (tmp_data.type != FREE) break;

    uint32_t tmp_size = block_align_up(parse_oct(tmp_data.size, sizeof(tmp_data)));
    space += BLOCK_SIZE + tmp_size;
    tmp_offset += BLOCK_SIZE + tmp_size;
  }

  if (offset > file_size) offset = file_size;

  uint32_t current_end = file_size;
  uint32_t new_end = (offset + size) > file_size ? (offset + size) : file_size;

  if (new_end <= current_end + space) {
    file_size = new_end;
    write_oct(data.size, file_size, sizeof(data.size));
    uint32_t write_size = fs_write(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
    CHECK_UNLOCK(write_size != BLOCK_SIZE, "Failed to update metadata.", 0);

    write_size = fs_write(
      self->block_device, disk_offset + BLOCK_SIZE + offset, size, buf
      );

    if (block_align_up(new_end) == block_align_up(current_end)) {
      kunlock(&(self->lock));
      return write_size;
    }

    uint32_t remaining_space =
      block_align_up(current_end + space) - block_align_up(new_end);

    if (remaining_space >= BLOCK_SIZE) {
      uint32_t free_block_size = remaining_space - BLOCK_SIZE;
      ustar_metadata_t free_block;
      u_memcpy(free_block.ustar_magic, USTAR_MAGIC, u_strlen(USTAR_MAGIC) + 1);
      free_block.type = FREE;
      write_oct(free_block.size, free_block_size, sizeof(free_block.size));

      uint32_t ws = fs_write(
        self->block_device,
        disk_offset + BLOCK_SIZE + block_align_up(new_end), BLOCK_SIZE,
        (uint8_t *)&free_block
        );
      CHECK_UNLOCK(ws != BLOCK_SIZE, "Failed to create free block", write_size);
    }

    kunlock(&(self->lock));
    return write_size;
  }

  uint32_t new_offset = ustar_alloc(self, new_end);
  CHECK_UNLOCK(new_offset == 0, "No disk space.", ENOSPC);
  // TODO copy the file in chunks if necessary
  uint8_t *new_buf = kmalloc(new_end);
  CHECK_UNLOCK(new_buf == NULL, "No memory.", ENOMEM);
  read_size = fs_read(self->block_device, disk_offset + BLOCK_SIZE, file_size, new_buf);
  CHECK_UNLOCK(read_size != file_size, "Failed to copy data.", 0);

  ustar_metadata_t new_data = data;
  write_oct(new_data.size, new_end, sizeof(new_data.size));
  uint32_t write_size = fs_write(
    self->block_device, new_offset, BLOCK_SIZE, (uint8_t *)&new_data
    );
  CHECK_UNLOCK(write_size != BLOCK_SIZE, "Failed to update metadata.", 0);

  u_memcpy(new_buf + offset, buf, size);
  write_size = fs_write(self->block_device, new_offset + BLOCK_SIZE, new_end, new_buf);
  kunlock(&(self->lock));
  return size - (new_end - write_size);
}

struct dirent *ustar_readdir(fs_node_t *node, uint32_t idx)
{
  // TODO
  return NULL;
}

// TODO other ops

uint32_t ustar_init(const char *dev_path)
{
  ustar_fs_t *fs = kmalloc(sizeof(ustar_fs_t));
  CHECK(fs == NULL, "No memory", ENOMEM);
  u_memset(fs, 0, sizeof(ustar_fs_t));

  fs->block_device = kmalloc(sizeof(fs_node_t));
  CHECK(fs->block_device == NULL, "No memory.", ENOMEM);
  uint32_t res = fs_open_node(fs->block_device, dev_path, 0);
  CHECK(res, "Failed to open block device.", res);

  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  CHECK(node == NULL, "No memory.", ENOMEM);
  u_memset(node, 0, sizeof(fs_node_t));

  node->device = fs;
  u_memcpy(node->name, USTAR_ROOT, u_strlen(USTAR_ROOT) + 1);
  res = fs_mount(node, USTAR_ROOT);
  CHECK(res, "Failed to mount at " USTAR_ROOT, res);

  return 0;
}
