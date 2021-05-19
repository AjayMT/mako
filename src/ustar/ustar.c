
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
#define FREE     '~'

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
  for (uint32_t i = 0; i < size && s[i] && s[i] != ' '; ++i) {
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

static int32_t ustar_name_match(char *data_name, char *name)
{
  uint32_t len = u_strlen(data_name);
  uint32_t name_len = u_strlen(name);
  if (data_name[len - 1] == FS_PATH_SEP && len == name_len + 1)
    return u_strncmp(data_name, name, len - 1);
  return u_strcmp(data_name, name);
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
    if (u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0) return -1;

    if (ustar_name_match(data.name, name) == 0 && data.type != FREE)
      return disk_offset;

    uint32_t size = block_align_up(parse_oct(data.size, sizeof(data.size)));
    disk_offset += BLOCK_SIZE + size;
  }

  return -1;
}

static uint32_t ustar_alloc(ustar_fs_t *self, uint32_t size)
{
  uint32_t disk_offset = 0;
  while (1) {
    ustar_metadata_t data;
    uint32_t read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );

    if (read_size != BLOCK_SIZE) return 0;
    if (u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0) return disk_offset;

    uint32_t block_size = block_align_up(parse_oct(data.size, sizeof(data.size)));
    if (data.type == FREE && block_size >= size) return disk_offset;

    disk_offset += BLOCK_SIZE + block_size;
  }
  return 0;
}

void ustar_open(fs_node_t *node, uint32_t flags)
{
  if ((flags & O_TRUNC) == 0) return;


  ustar_fs_t *self = node->device;
  uint32_t disk_offset = node->inode;
  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  if (read_size != BLOCK_SIZE || u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0) {
    log_error("ustar", "Failed to read metadata.\n");
    return;
  }
  if (
    u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0
    || ustar_name_match(data.name, node->name) != 0
    ) {
    disk_offset = ustar_find(self, node->name);
    if (disk_offset == (uint32_t)-1) {
      log_error("ustar", "Failed to read metadata.\n");
      return;
    }
    read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
  }

  uint32_t size = parse_oct(data.size, sizeof(data.size));
  if (size == 0) return;

  klock(&(self->lock));

  uint32_t new_size = block_align_up(size) - BLOCK_SIZE;
  write_oct(data.size, 0, sizeof(data.size));
  uint32_t write_size = fs_write(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  if (write_size != BLOCK_SIZE) {
    log_error("ustar", "Failed to update metadata.\n");
    kunlock(&(self->lock));
    return;
  }
  ustar_metadata_t new_data = data;
  u_memcpy(new_data.ustar_magic, USTAR_MAGIC, u_strlen(USTAR_MAGIC) + 1);
  write_oct(new_data.size, new_size, sizeof(new_data.size));
  new_data.type = FREE;
  write_size = fs_write(
    self->block_device, disk_offset + BLOCK_SIZE, BLOCK_SIZE, (uint8_t *)&new_data
    );

  kunlock(&(self->lock));

  if (write_size != BLOCK_SIZE) {
    log_error("ustar", "Failed to update metadata.\n");
    return;
  }
}

void ustar_close(fs_node_t *node)
{}

uint32_t ustar_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  ustar_fs_t *self = node->device;
  uint32_t disk_offset = node->inode;
  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);

  if (
    u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0
    || ustar_name_match(data.name, node->name) != 0
    ) {
    disk_offset = ustar_find(self, node->name);
    CHECK(disk_offset == (uint32_t)-1, "File does not exist.", ENOENT);
    read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
    CHECK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);
  }

  uint32_t file_size = parse_oct(data.size, sizeof(data.size));
  if (offset > file_size) return 0;
  if (offset + size > file_size) size = file_size - offset;
  return fs_read(self->block_device, disk_offset + BLOCK_SIZE + offset, size, buf);
}

uint32_t ustar_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  ustar_fs_t *self = node->device;
  klock(&(self->lock));

  uint32_t disk_offset = node->inode;
  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);

  if (
    u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0
    || ustar_name_match(data.name, node->name) != 0
    ) {
    disk_offset = ustar_find(self, node->name);
    CHECK_UNLOCK(disk_offset == (uint32_t)-1, "File does not exist.", ENOENT);
    read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
    CHECK_UNLOCK(read_size != BLOCK_SIZE, "File does not exist.", ENOENT);
  }

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

  data.type = FREE;
  uint32_t ws = fs_write(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(ws != BLOCK_SIZE, "Failed to update metadata.", write_size);

  kunlock(&(self->lock));
  return size - (new_end - write_size);
}

struct dirent *ustar_readdir(fs_node_t *node, uint32_t idx)
{
  ustar_fs_t *self = node->device;
  struct dirent *ent = kmalloc(sizeof(struct dirent));
  CHECK(ent == NULL, "No memory.", NULL);
  u_memset(ent, 0, sizeof(struct dirent));

  // don't create "." and ".." entries for root directory
  if (node->inode != 0) {
    if (idx < 2) {
      char *name = idx == 0 ? "." : "..";
      u_memcpy(ent->name, name, u_strlen(name) + 1);
      ent->ino = idx == 0 ? node->inode : 0;
      return ent;
    }
    idx -= 2;
  }

  uint32_t i = 0;
  uint32_t disk_offset = 0;
  while (1) {
    ustar_metadata_t data;
    uint32_t read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
    if (read_size != BLOCK_SIZE) return NULL;
    if (u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0) return NULL;

    uint32_t ent_offset = disk_offset;
    disk_offset += BLOCK_SIZE + block_align_up(parse_oct(data.size, sizeof(data.size)));

    if (data.type == FREE) continue;

    uint32_t data_name_len = u_strlen(data.name);
    if (data_name_len == 1) continue; // root dir

    uint32_t end_idx = data_name_len;
    uint32_t basename_idx = data_name_len - 1;
    if (data.name[basename_idx] == FS_PATH_SEP) { --basename_idx; --end_idx; }
    while (data.name[basename_idx] != FS_PATH_SEP) --basename_idx;
    ++basename_idx;
    uint32_t name_len = u_strlen(node->name);

    if (
      u_strncmp(data.name, node->name, basename_idx) == 0
      && basename_idx == name_len
      )
      ++i;

    if (i <= idx) continue;

    u_memcpy(ent->name, data.name + basename_idx, end_idx - basename_idx);
    ent->ino = ent_offset;
    return ent;
  }

  kfree(ent);
  return NULL;
}

static void make_ustar_node(ustar_fs_t *, uint32_t, ustar_metadata_t, fs_node_t *);

fs_node_t *ustar_finddir(fs_node_t *node, char *name)
{
  ustar_fs_t *self = node->device;

  char path[100];
  u_memset(path, 0, sizeof(path));
  uint32_t len = u_strlen(node->name);
  u_memcpy(path, node->name, len);
  if (path[len - 1] != FS_PATH_SEP) {
    path[len] = FS_PATH_SEP;
    ++len;
  }
  u_memcpy(path + len, name, u_strlen(name));

  uint32_t disk_offset = ustar_find(self, path);
  if (disk_offset == (uint32_t)-1) return NULL;
  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK(read_size != BLOCK_SIZE, "Failed to read metadata.", NULL);

  fs_node_t *out = kmalloc(sizeof(fs_node_t));
  CHECK(out == NULL, "No memory.", NULL);
  make_ustar_node(self, disk_offset, data, out);

  return out;
}

int32_t ustar_create_entry(
  fs_node_t *node, char *name, uint16_t mask, char type, char *linked
  )
{
  ustar_fs_t *self = node->device;
  fs_node_t *existing = ustar_finddir(node, name);
  if (existing) {
    kfree(existing); return -EEXIST;
  }

  char path[100];
  u_memset(path, 0, sizeof(path));
  uint32_t len = u_strlen(node->name);
  u_memcpy(path, node->name, len);
  if (path[len - 1] != FS_PATH_SEP) {
    path[len] = FS_PATH_SEP;
    ++len;
  }
  uint32_t name_len = u_strlen(name);
  u_memcpy(path + len, name, name_len);
  if (type == DIR) path[len + name_len] = FS_PATH_SEP;

  ustar_metadata_t data;
  u_memset(&data, 0, sizeof(data));
  u_memcpy(data.name, path, u_strlen(path) + 1);
  u_memcpy(data.ustar_magic, USTAR_MAGIC, u_strlen(USTAR_MAGIC) + 1);
  if (linked != NULL)
    u_memcpy(data.linked_name, linked, u_strlen(linked) + 1);
  write_oct(data.size, 0, sizeof(data.size));
  data.type = type;

  klock(&(self->lock));
  uint32_t disk_offset = ustar_alloc(self, 0);
  CHECK_UNLOCK(disk_offset == 0, "No space.", -ENOSPC);

  uint32_t write_size = fs_write(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(write_size != BLOCK_SIZE, "Failed to write metadata.", -ENOSPC);

  kunlock(&(self->lock));
  return 0;
}

int32_t ustar_mkdir(fs_node_t *node, char *name, uint16_t mask)
{ return ustar_create_entry(node, name, mask, DIR, NULL); }
int32_t ustar_create(fs_node_t *node, char *name, uint16_t mask)
{ return ustar_create_entry(node, name, mask, NORMAL, NULL); }
int32_t ustar_symlink(fs_node_t *node, char *src, char *dst)
{ return ustar_create_entry(node, dst, 0, SYMLINK, src); }

int32_t ustar_unlink(fs_node_t *node, char *name)
{
  ustar_fs_t *self = node->device;
  fs_node_t *child = ustar_finddir(node, name);
  if (child == NULL) return -ENOENT;
  uint32_t disk_offset = child->inode;
  kfree(child);

  // TODO coalesce free blocks

  klock(&(self->lock));
  ustar_metadata_t data;
  uint32_t r = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(r != BLOCK_SIZE, "Failed to read metadata.", -ENOENT);
  data.type = FREE;
  r = fs_write(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(r != BLOCK_SIZE, "Failed to write metadata.", -ENOENT);

  kunlock(&(self->lock));
  return 0;
}

int32_t ustar_readlink(fs_node_t *node, char *buf, size_t len)
{
  ustar_fs_t *self = node->device;
  uint32_t disk_offset = node->inode;
  ustar_metadata_t data;
  uint32_t read_size = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK(read_size != BLOCK_SIZE, "File does not exist.", -ENOENT);

  if (
    u_strcmp(data.ustar_magic, USTAR_MAGIC) != 0
    || ustar_name_match(data.name, node->name) != 0
    ) {
    disk_offset = ustar_find(self, node->name);
    CHECK(disk_offset == (uint32_t)-1, "File does not exist.", -ENOENT);
    read_size = fs_read(
      self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
      );
    CHECK(read_size != BLOCK_SIZE, "File does not exist.", -ENOENT);
  }

  uint32_t linked_name_len = u_strlen(data.linked_name);
  uint32_t min = linked_name_len < len ? linked_name_len : len;
  u_memcpy(buf, data.linked_name, min);

  return min;
}

int32_t ustar_rename(fs_node_t *node, char *from, char *to)
{
  ustar_fs_t *self = node->device;
  fs_node_t *child = ustar_finddir(node, from);
  if (child == NULL) return -ENOENT;
  fs_node_t *dst = ustar_finddir(node, to);
  if (dst) return -EEXIST;

  char path[100];
  u_memset(path, 0, sizeof(path));
  uint32_t len = u_strlen(node->name);
  u_memcpy(path, node->name, len);
  if (path[len - 1] != FS_PATH_SEP) {
    path[len] = FS_PATH_SEP;
    ++len;
  }
  u_memcpy(path + len, to, u_strlen(to));

  uint32_t disk_offset = child->inode;
  kfree(child);

  klock(&(self->lock));
  ustar_metadata_t data;
  uint32_t r = fs_read(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(r != BLOCK_SIZE, "Failed to read metadata.", -ENOENT);
  u_memcpy(data.name, path, u_strlen(path) + 1);
  r = fs_write(
    self->block_device, disk_offset, BLOCK_SIZE, (uint8_t *)&data
    );
  CHECK_UNLOCK(r != BLOCK_SIZE, "Failed to write metadata.", -ENOENT);

  kunlock(&(self->lock));
  return 0;
}

static void make_ustar_node(
  ustar_fs_t *self, uint32_t disk_offset, ustar_metadata_t data, fs_node_t *out
  )
{
  uint32_t file_size = parse_oct(data.size, sizeof(data.size));
  u_memset(out, 0, sizeof(fs_node_t));
  u_memcpy(out->name, data.name, u_strlen(data.name) + 1);
  out->inode = disk_offset;
  out->device = self;
  out->length = file_size;
  out->open = ustar_open;
  out->close = ustar_close;
  if (data.type == NORMAL || data.type == NORMAL_) {
    out->flags |= FS_FILE;
    out->read = ustar_read;
    out->write = ustar_write;
  } else if (data.type == DIR) {
    out->flags |= FS_DIRECTORY;
    out->readdir = ustar_readdir;
    out->finddir = ustar_finddir;
    out->create = ustar_create;
    out->mkdir = ustar_mkdir;
    out->unlink = ustar_unlink;
    out->symlink = ustar_symlink;
  } else if (data.type == SYMLINK) {
    out->flags |= FS_SYMLINK;
    out->readlink = ustar_readlink;
  }
  out->rename = ustar_rename;
}

uint32_t ustar_init(const char *dev_path)
{
  ustar_fs_t *fs = kmalloc(sizeof(ustar_fs_t));
  CHECK(fs == NULL, "No memory", ENOMEM);
  u_memset(fs, 0, sizeof(ustar_fs_t));

  fs->block_device = kmalloc(sizeof(fs_node_t));
  CHECK(fs->block_device == NULL, "No memory.", ENOMEM);
  uint32_t res = fs_open_node(fs->block_device, dev_path, 0);
  CHECK(res, "Failed to open block device.", res);

  ustar_metadata_t data;
  uint32_t read_size = fs_read(fs->block_device, 0, BLOCK_SIZE, (uint8_t *)&data);
  CHECK(read_size != BLOCK_SIZE, "Failed to read root metadata.", -1);

  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  CHECK(node == NULL, "No memory.", ENOMEM);
  make_ustar_node(fs, 0, data, node);

  res = fs_mount(node, USTAR_ROOT);
  CHECK(res, "Failed to mount at " USTAR_ROOT, res);

  return 0;
}
