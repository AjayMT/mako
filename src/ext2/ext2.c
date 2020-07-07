
// ext2.c
//
// Ext2 filesystem implementation.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <kheap/kheap.h>
#include <klock/klock.h>
#include <process/process.h>
#include <fs/fs.h>
#include <util/util.h>
#include <common/errno.h>
#include <debug/log.h>
#include "ext2.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("ext2", msg "\n"); return (code); \
  }
#define CHECK_UNLOCK_I(err, msg, code) if ((err)) {             \
    log_error("ext2", msg "\n"); kunlock(&(self->inode_lock));  \
    return (code);                                              \
  }
#define CHECK_UNLOCK_B(err, msg, code) if ((err)) {             \
    log_error("ext2", msg "\n"); kunlock(&(self->block_lock));  \
    return (code);                                              \
  }
#define CHECK_UNLOCK_O(err, msg, code) if ((err)) {             \
    log_error("ext2", msg "\n"); kunlock(&(self->ops_lock));    \
    return (code);                                              \
  }

typedef struct ext2_fs_s {
  fs_node_t *block_device;
  ext2_superblock_t *superblock;
  ext2_bgd_t *bgds;
  uint32_t block_size;
  uint32_t blocks_per_group;
  uint32_t inodes_per_group;
  uint32_t group_count;
  uint32_t bgd_block_count;
  volatile uint32_t lock;
  volatile uint32_t inode_lock;
  volatile uint32_t block_lock;
  volatile uint32_t bgds_lock;
  volatile uint32_t ops_lock;
} ext2_fs_t;

static const uint32_t EXT2_DIRECT_BLOCKS = 12;
static const uint16_t EXT2_MAGIC         = 0xEF53;

static inline uint8_t blockbyte(uint8_t *buf, uint32_t n)
{ return buf[n >> 3]; }
static inline uint8_t setbit(uint32_t n)
{ return (1 << (n % 8)); }
static inline uint8_t blockbit(uint8_t *buf, uint32_t n)
{ return blockbyte(buf, n) & setbit(n); }

static void make_ext2_node(
  ext2_fs_t *, fs_node_t *, ext2_inode_t *, ext2_dir_entry_t *
  );

static uint32_t read_block(
  ext2_fs_t *self, uint32_t block_num, uint8_t *buf
  )
{
  klock(&(self->lock));
  uint32_t res = fs_read(
    self->block_device,
    self->block_size * block_num,
    self->block_size,
    buf
    );
  kunlock(&(self->lock));
  return res;
}

static uint32_t write_block(
  ext2_fs_t *self, uint32_t block_num, uint8_t *buf
  )
{
  klock(&(self->lock));
  uint32_t res = fs_write(
    self->block_device,
    self->block_size * block_num,
    self->block_size,
    buf
    );
  kunlock(&(self->lock));
  return res;
}

static uint32_t write_bgds(ext2_fs_t *self)
{
  klock(&(self->bgds_lock));

  uint32_t bgd_block_offset = self->block_size > 1024 ? 1 : 2;
  for (uint32_t i = 0; i < self->bgd_block_count; ++i) {
    uint32_t res = write_block(
      self,
      bgd_block_offset + i,
      (uint8_t *)((uint32_t)self->bgds + (i * self->block_size))
      );
    if (res != self->block_size) {
      log_error("ext2", "Failed to write block group descriptors.");
      kunlock(&(self->bgds_lock));
      return EAGAIN;
    }
  }

  kunlock(&(self->bgds_lock));
  return 0;
}

static uint32_t alloc_block(ext2_fs_t *self)
{
  klock(&(self->block_lock));

  uint32_t res = 0;
  uint32_t block_num = 0;
  uint32_t block_offset = 0;
  uint32_t group_num = 0;
  uint8_t *bg_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_B(bg_buf == NULL, "No memory.", 0);
  for (uint32_t i = 0; i < self->group_count; ++i) {
    if (
      self->bgds[i].free_block_count <= 0
      || self->bgds[i].free_block_count >= (self->block_size << 3)
      ) continue;

    res = read_block(self, self->bgds[i].block_bitmap, bg_buf);
    CHECK_UNLOCK_B(res != self->block_size, "Failed to read block.", 0);
    while (blockbit(bg_buf, block_offset)) ++block_offset;
    block_num = block_offset + (self->blocks_per_group * i);
    group_num = i;
    break;
  }

  if (block_num == 0) { kfree(bg_buf); return 0; }

  bg_buf[block_offset >> 3] |= setbit(block_offset);
  res = write_block(self, self->bgds[group_num].block_bitmap, bg_buf);
  CHECK_UNLOCK_B(res != self->block_size, "Failed to write block.", 0);
  --(self->bgds[group_num].free_block_count);
  res = write_bgds(self);
  CHECK_UNLOCK_B(res, "Failed to write block group descriptors.", res);

  --(self->superblock->free_block_count);
  res = fs_write(
    self->block_device,
    1024,
    sizeof(ext2_superblock_t),
    (uint8_t *)self->superblock
    );
  CHECK_UNLOCK_B(
    res != sizeof(ext2_superblock_t), "Failed to write superblock.", 0
    );

  u_memset(bg_buf, 0, self->block_size);
  res = write_block(self, block_num, bg_buf);
  CHECK_UNLOCK_B(res != self->block_size, "Failed to clear block.", 0);

  kfree(bg_buf);
  kunlock(&(self->block_lock));
  return block_num;
}

static uint32_t alloc_inode(ext2_fs_t *self)
{
  klock(&(self->inode_lock));

  uint32_t res = 0;
  uint32_t inode_num = 0;
  uint32_t inode_offset = 0;
  uint32_t group_num = 0;
  uint8_t *bg_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_I(bg_buf == NULL, "No memory.", 0);
  for (uint32_t i = 0; i < self->group_count; ++i) {
    if (self->bgds[i].free_block_count == 0) continue;

    res = read_block(self, self->bgds[i].inode_bitmap, bg_buf);
    CHECK_UNLOCK_I(res != self->block_size, "Failed to read block.", 0);
    while (blockbit(bg_buf, inode_offset)) ++inode_offset;
    inode_num = inode_offset + (self->inodes_per_group * i);
    group_num = i;
    break;
  }

  if (inode_num == 0) { kfree(bg_buf); return 0; }

  bg_buf[inode_offset >> 3] |= 1 << (inode_offset % 8);
  res = write_block(self, self->bgds[group_num].inode_bitmap, bg_buf);
  CHECK_UNLOCK_I(res != self->block_size, "Failed to write block.", 0);
  --(self->bgds[group_num].free_inode_count);
  res = write_bgds(self);
  CHECK_UNLOCK_I(res, "Failed to write block group descriptors.", res);

  --(self->superblock->free_inode_count);
  res = fs_write(
    self->block_device,
    1024,
    sizeof(ext2_superblock_t),
    (uint8_t *)self->superblock
    );
  CHECK_UNLOCK_I(
    res != sizeof(ext2_superblock_t), "Failed to write superblock.", 0
    );

  kfree(bg_buf);
  kunlock(&(self->inode_lock));
  return inode_num;
}

static uint32_t free_block(ext2_fs_t *self, uint32_t block_num)
{
  klock(&(self->block_lock));

  uint32_t group_num = block_num / self->blocks_per_group;
  uint32_t bitmap_num = (block_num % self->blocks_per_group) / 4;
  uint32_t idx = (block_num % self->blocks_per_group) % 4;
  uint32_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_B(blk_buf == NULL, "No memory.", ENOMEM);
  uint32_t res = read_block(
    self, self->bgds[group_num].block_bitmap, (uint8_t *)blk_buf
    );
  CHECK_UNLOCK_B(
    res != self->block_size, "Failed to read disk block.", EAGAIN
    );

  uint32_t mask = ~(1 << idx);
  blk_buf[bitmap_num] = blk_buf[bitmap_num] & mask;

  res = write_block(
    self, self->bgds[group_num].block_bitmap, (uint8_t *)blk_buf
    );
  CHECK_UNLOCK_B(
    res != self->block_size, "Failed to write disk block.", EAGAIN
    );
  ++(self->bgds[group_num].free_block_count);
  res = write_bgds(self);
  CHECK_UNLOCK_B(res, "Failed to write block group descriptors.", res);

  ++(self->superblock->free_block_count);
  res = fs_write(
    self->block_device,
    1024,
    sizeof(ext2_superblock_t),
    (uint8_t *)self->superblock
    );
  CHECK_UNLOCK_B(
    res != sizeof(ext2_superblock_t), "Failed to write superblock.", EAGAIN
    );

  kunlock(&(self->block_lock));
  return 0;
}

static uint32_t free_inode(ext2_fs_t *self, uint32_t inode_num)
{
  klock(&(self->inode_lock));

  uint32_t group_num = inode_num / self->inodes_per_group;
  uint32_t bitmap_num = (inode_num % self->inodes_per_group) / 4;
  uint32_t idx = (inode_num % self->inodes_per_group) % 4;
  uint32_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_B(blk_buf == NULL, "No memory.", ENOMEM);
  uint32_t res = read_block(
    self, self->bgds[group_num].inode_bitmap, (uint8_t *)blk_buf
    );
  CHECK_UNLOCK_B(
    res != self->block_size, "Failed to read disk block.", EAGAIN
    );

  uint32_t mask = ~(1 << idx);
  blk_buf[bitmap_num] = blk_buf[bitmap_num] & mask;

  res = write_block(
    self, self->bgds[group_num].inode_bitmap, (uint8_t *)blk_buf
    );
  CHECK_UNLOCK_B(
    res != self->block_size, "Failed to write disk block.", EAGAIN
    );
  ++(self->bgds[group_num].free_inode_count);
  res = write_bgds(self);
  CHECK_UNLOCK_I(res, "Failed to write block group descriptors.", res);

  ++(self->superblock->free_inode_count);
  res = fs_write(
    self->block_device,
    1024,
    sizeof(ext2_superblock_t),
    (uint8_t *)self->superblock
    );
  CHECK_UNLOCK_I(
    res != sizeof(ext2_superblock_t), "Failed to write superblock.", EAGAIN
    );

  kunlock(&(self->inode_lock));
  return 0;
}

static uint32_t read_inode_info(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t inode_num
  )
{
  uint32_t group_idx = inode_num / self->inodes_per_group;
  CHECK(group_idx > self->group_count, "Invalid group number.", EAGAIN);
  uint32_t inode_table_block = self->bgds[group_idx].inode_table;
  uint32_t group_offset = inode_num % self->inodes_per_group;
  // group_offset - 1 since inode numbers start at 1.
  uint32_t block_idx = ((group_offset - 1) * self->superblock->inode_size)
    / self->block_size;
  uint32_t block_offset = (group_offset - 1)
    - (block_idx * (self->block_size / self->superblock->inode_size));

  uint8_t *buf = kmalloc(self->block_size);
  CHECK(buf == NULL, "No memory.", ENOMEM);
  uint32_t res = read_block(self, inode_table_block + block_idx, buf);
  CHECK(res != self->block_size, "Failed to read block.", res);
  u_memcpy(
    inode,
    buf + (block_offset * self->superblock->inode_size),
    self->superblock->inode_size
    );
  kfree(buf);

  return 0;
}

static uint32_t write_inode_info(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t inode_num
  )
{
  uint32_t group_idx = inode_num / self->inodes_per_group;
  CHECK(group_idx > self->group_count, "Invalid group number.", EAGAIN);
  uint32_t inode_table_block = self->bgds[group_idx].inode_table;
  uint32_t group_offset = inode_num % self->inodes_per_group;
  // group_offset - 1 since inode numbers start at 1.
  uint32_t block_idx = ((group_offset - 1) * self->superblock->inode_size)
    / self->block_size;
  uint32_t block_offset = (group_offset - 1)
    - (block_idx * (self->block_size / self->superblock->inode_size));

  uint8_t *buf = kmalloc(self->block_size);
  CHECK(buf == NULL, "No memory.", ENOMEM);
  uint32_t res = read_block(self, inode_table_block + block_idx, buf);
  CHECK(res != self->block_size, "Failed to read block.", res);
  u_memcpy(
    buf + (block_offset * self->superblock->inode_size),
    inode,
    self->superblock->inode_size
    );
  res = write_block(self, inode_table_block + block_idx, buf);
  CHECK(res != self->block_size, "Failed to write block.", res);
  kfree(buf);

  return 0;
}

static uint32_t get_disk_block_number(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t block_num
  )
{
  uint32_t p = self->block_size / 4;
  uint32_t res = 0;
  int32_t a, b, c, d, e, f, g; // Some jugaad happens here.
  uint32_t *tmp = kmalloc(self->block_size);
  CHECK(tmp == NULL, "No memory.", -ENOMEM);

  a = block_num - EXT2_DIRECT_BLOCKS;
  if (a < 0) {
    kfree(tmp);
    return inode->block_pointer[block_num];
  }

  b = a - p;
  if (b < 0) {
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = tmp[a];
    kfree(tmp);
    return res;
  }

  c = b - (p * p);
  if (c < 0) {
    c = b / p;
    d = b - (c * p);
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 1], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = read_block(self, tmp[c], (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = tmp[d];
    kfree(tmp);
    return res;
  }

  d = c - (p * p * p);
  if (d < 0) {
    e = c / (p * p);
    f = (c - (e * p * p)) / p;
    g = (c - (e * p * p) - (f * p));
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 2], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = read_block(self, tmp[e], (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = read_block(self, tmp[f], (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", -1);
    res = tmp[g];
    kfree(tmp);
    return res;
  }

  kfree(tmp);
  return 0;
}

static uint32_t set_disk_block_number(
  ext2_fs_t *self,
  ext2_inode_t *inode,
  uint32_t inode_num,
  uint32_t inode_block_num,
  uint32_t disk_block_num
  )
{
  // This is beyond science.

  uint32_t p = self->block_size / 4;
  uint32_t res = 0;
  int32_t a, b, c, d, e, f, g;
  uint32_t *tmp = kmalloc(self->block_size);
  CHECK(tmp == NULL, "No memory.", ENOMEM);

  a = inode_block_num - EXT2_DIRECT_BLOCKS;
  if (a <= 0) {
    inode->block_pointer[inode_block_num] = disk_block_num;
    kfree(tmp);
    return 0;
  }

  b = a - p;
  if (b <= 0) {
    if (inode->block_pointer[EXT2_DIRECT_BLOCKS] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      inode->block_pointer[EXT2_DIRECT_BLOCKS] = bn;
      res = write_inode_info(self, inode, inode_num);
      CHECK(res, "Failed to write inode info.", res);
    }
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    tmp[a] = disk_block_num;
    res = write_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    kfree(tmp);
    return 0;
  }

  c = b - (p * p);
  if (c <= 0) {
    c = b / p;
    d = b - (c * p);
    if (inode->block_pointer[EXT2_DIRECT_BLOCKS + 1] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      inode->block_pointer[EXT2_DIRECT_BLOCKS + 1] = bn;
      res = write_inode_info(self, inode, inode_num);
      CHECK(res, "Failed to write inode info.", res);
    }
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 1], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    if (tmp[c] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      tmp[c] = bn;
      res = write_block(
        self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 1], (uint8_t *)tmp
        );
      CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    }
    uint32_t bn = tmp[c];
    res = read_block(self, bn, (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    tmp[d] = disk_block_num;
    res = write_block(self, bn, (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    kfree(tmp);
    return 0;
  }

  d = c - (p * p * p);
  if (d <= 0) {
    e = c / (p * p);
    f = (c - (e * p * p)) / p;
    g = (c - (e * p * p) - (f * p));
    if (inode->block_pointer[EXT2_DIRECT_BLOCKS + 2] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      inode->block_pointer[EXT2_DIRECT_BLOCKS + 2] = bn;
      res = write_inode_info(self, inode, inode_num);
      CHECK(res, "Failed to write inode info.", res);
    }
    res = read_block(
      self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 2], (uint8_t *)tmp
      );
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    if (tmp[e] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      tmp[e] = bn;
      res = write_block(
        self, inode->block_pointer[EXT2_DIRECT_BLOCKS + 2], (uint8_t *)tmp
        );
      CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    }
    uint32_t nblock = tmp[e];
    res = read_block(self, nblock, (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    if (tmp[f] == 0) {
      uint32_t bn = alloc_block(self);
      CHECK(bn == 0, "No space.", ENOSPC);
      tmp[f] = bn;
      res = write_block(self, nblock, (uint8_t *)tmp);
      CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    }
    nblock = tmp[f];
    res = read_block(self, nblock, (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to read block.", EAGAIN);
    tmp[g] = disk_block_num;
    res = write_block(self, nblock, (uint8_t *)tmp);
    CHECK(res != self->block_size, "Failed to write block.", EAGAIN);
    kfree(tmp);
    return 0;
  }

  kfree(tmp);
  return res;
}

static uint32_t alloc_inode_block(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t inode_num, uint32_t block_num
  )
{
  uint32_t disk_block_num = alloc_block(self);
  CHECK(disk_block_num == 0, "No space.", ENOSPC);

  uint32_t res = set_disk_block_number(
    self, inode, inode_num, block_num, disk_block_num
    );
  CHECK(res, "Failed to set block number.", res);

  uint32_t tmp = (block_num + 1) * (self->block_size / 512);
  if (inode->sector_count < tmp) inode->sector_count = tmp;

  res = write_inode_info(self, inode, inode_num);
  CHECK(res, "Failed to write inode info.", res);

  return 0;
}

static uint32_t free_inode_block(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t inode_num, uint32_t block_num
  )
{
  uint32_t disk_block_num = get_disk_block_number(self, inode, block_num);
  CHECK(
    disk_block_num >= (uint32_t)(-ENOMEM),
    "Failed to get disk block number.",
    EAGAIN
    );
  uint32_t res = free_block(self, disk_block_num);
  CHECK(res, "Failed to free disk block.", res);
  res = set_disk_block_number(self, inode, inode_num, block_num, 0);
  CHECK(res, "Failed to set disk block number.", res);
  return write_inode_info(self, inode, inode_num);
}

static uint32_t read_inode_block(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t block_num, uint8_t *buf
  )
{
  uint32_t disk_block_num = get_disk_block_number(self, inode, block_num);
  CHECK(
    disk_block_num >= (uint32_t)(-ENOMEM),
    "Failed to get disk block number.",
    0
    );
  return read_block(self, disk_block_num, buf);
}

static uint32_t write_inode_block(
  ext2_fs_t *self,
  ext2_inode_t *inode,
  uint32_t inode_num,
  uint32_t block_num,
  uint8_t *buf
  )
{
  uint32_t res = 0;
  while (block_num >= inode->sector_count / (self->block_size / 512)) {
    res = alloc_inode_block(
      self, inode, inode_num, inode->sector_count / (self->block_size / 512)
      );
    CHECK(res, "Failed to allocate block.", 0);
    res = read_inode_info(self, inode, inode_num);
    CHECK(res, "Failed to read inode info.", 0);
  }

  uint32_t disk_block_num = get_disk_block_number(self, inode, block_num);
  CHECK(
    disk_block_num >= (uint32_t)(-ENOMEM),
    "Failed to get disk block number.",
    0
    );

  return write_block(self, disk_block_num, buf);
}

static int32_t create_dir_entry(
  fs_node_t *node, char *name, uint32_t inode_num
  )
{
  ext2_fs_t *self = node->device;
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK(res, "Failed to read inode info.", -res);
  if ((inode.permissions & EXT2_S_IFDIR) == 0)
    return -ENOTDIR;

  uint32_t ent_size = sizeof(ext2_dir_entry_t) + u_strlen(name);
  if (ent_size % 4) ent_size += 4 - (ent_size % 4);
  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK(blk_buf == NULL, "No memory.", -ENOMEM);
  uint32_t block_num = 0;
  res = read_inode_block(self, &inode, block_num, blk_buf);
  CHECK(res != self->block_size, "Failed to read inode block.", -EAGAIN);

  uint32_t idx = 0;
  uint32_t dir_idx = 0;
  ext2_dir_entry_t *current_entry = NULL;
  for (
    ;
    idx < inode.size;
    idx += current_entry->size, dir_idx += current_entry->size
    )
  {
    if (dir_idx >= self->block_size) {
      ++block_num;
      dir_idx = 0;
      res = read_inode_block(self, &inode, block_num, blk_buf);
      CHECK(res != self->block_size, "Failed to read inode block.", -EAGAIN);
    }

    current_entry = (ext2_dir_entry_t *)(blk_buf + dir_idx);
    uint32_t e_size = sizeof(ext2_dir_entry_t) + current_entry->name_len;
    if (e_size % 4) e_size += 4 - (e_size % 4);
    if (
      current_entry->size != e_size && idx + current_entry->size == inode.size
      )
    {
      idx += e_size;
      dir_idx += e_size;
      if (dir_idx + ent_size >= self->block_size) {
        kfree(blk_buf); return -ENOSPC;
      }
      current_entry->size = e_size;
      break;
    }
  }

  current_entry = (ext2_dir_entry_t *)((uint32_t)blk_buf + dir_idx);
  current_entry->inode = inode_num;
  current_entry->size = self->block_size - dir_idx;
  current_entry->name_len = u_strlen(name);
  current_entry->type = 0;
  u_memcpy(current_entry->name, name, current_entry->name_len);

  res = write_inode_block(self, &inode, node->inode, block_num, blk_buf);
  CHECK(res != self->block_size, "Failed to write inode block", -EAGAIN);

  dir_idx += current_entry->size;
  if (dir_idx >= self->block_size) {
    ++block_num;
    dir_idx = 0;
    res = read_inode_block(self, &inode, block_num, blk_buf);
    CHECK(res != self->block_size, "Failed to read inode block.", -EAGAIN);
  }
  current_entry = (ext2_dir_entry_t *)((uint32_t)blk_buf + dir_idx);
  u_memset(current_entry, 0, sizeof(ext2_dir_entry_t));

  res = write_inode_block(self, &inode, node->inode, block_num, blk_buf);
  CHECK(res != self->block_size, "Failed to write inode block", -EAGAIN);

  kfree(blk_buf);
  return 0;
}

static struct dirent *ext2_readdir_inode(
  ext2_fs_t *self, ext2_inode_t *inode, uint32_t idx
  )
{
  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK(blk_buf == NULL, "No memory.", NULL);
  uint32_t block_num = 0;
  uint32_t res = read_inode_block(self, inode, block_num, blk_buf);
  CHECK(res != self->block_size, "Failed to read inode block.", NULL);

  uint32_t t_idx = 0;
  uint32_t dir_idx_w = 0;
  uint32_t dir_idx = 0;
  ext2_dir_entry_t *current_entry = NULL;
  ext2_dir_entry_t *found_entry = NULL;
  for (
    ;
    t_idx < inode->size && dir_idx <= idx;
    t_idx += current_entry->size, dir_idx_w += current_entry->size
    )
  {
    if (dir_idx_w >= self->block_size) {
      ++block_num;
      dir_idx_w -= self->block_size;
      res = read_inode_block(self, inode, block_num, blk_buf);
      CHECK(res != self->block_size, "Failed to read inode block.", NULL);
    }

    current_entry = (ext2_dir_entry_t *)(blk_buf + dir_idx_w);
    if (current_entry->inode && dir_idx == idx) {
      found_entry = kmalloc(current_entry->size);
      CHECK(found_entry == NULL, "No memory.", NULL);
      u_memcpy(found_entry, current_entry, current_entry->size);
      break;
    }
    if (current_entry->inode) ++dir_idx;
  }

  kfree(blk_buf);

  if (found_entry == NULL) return NULL;

  struct dirent *ent = kmalloc(sizeof(struct dirent));
  u_memcpy(ent->name, found_entry->name, found_entry->name_len);
  ent->name[found_entry->name_len] = '\0';
  ent->ino = found_entry->inode;

  kfree(found_entry);
  return ent;
}

static struct dirent *ext2_readdir(fs_node_t *node, uint32_t idx)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", NULL);
  if ((inode.permissions & EXT2_S_IFDIR) == 0) {
    kunlock(&(self->ops_lock));
    return NULL;
  }

  struct dirent *ent = ext2_readdir_inode(self, &inode, idx);
  kunlock(&(self->ops_lock));
  return ent;
}

static fs_node_t *ext2_finddir(fs_node_t *node, char *name)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", NULL);
  if ((inode.permissions & EXT2_S_IFDIR) == 0) {
    kunlock(&(self->ops_lock));
    return NULL;
  }

  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(blk_buf == NULL, "No memory.", NULL);
  uint32_t block_num = 0;
  res = read_inode_block(self, &inode, block_num, blk_buf);
  CHECK_UNLOCK_O(
    res != self->block_size, "Failed to read inode block.", NULL
    );

  uint32_t idx = 0;
  uint32_t dir_idx = 0;
  ext2_dir_entry_t *current_entry = NULL;
  ext2_dir_entry_t *found_entry = NULL;
  for (
    ;
    idx < inode.size;
    idx += current_entry->size, dir_idx += current_entry->size
    )
  {
    if (dir_idx >= self->block_size) {
      ++block_num;
      dir_idx -= self->block_size;
      res = read_inode_block(self, &inode, block_num, blk_buf);
      CHECK_UNLOCK_O(
        res != self->block_size, "Failed to read inode block.", NULL
        );
    }

    current_entry = (ext2_dir_entry_t *)(blk_buf + dir_idx);
    if (current_entry->inode == 0 || u_strlen(name) != current_entry->name_len)
      continue;

    char *dname = kmalloc(current_entry->name_len + 1);
    u_memcpy(dname, current_entry->name, current_entry->name_len);
    dname[current_entry->name_len] = '\0';
    if (u_strcmp(dname, name) == 0) {
      kfree(dname);
      found_entry = kmalloc(current_entry->size);
      CHECK_UNLOCK_O(found_entry == NULL, "No memory.", NULL);
      u_memcpy(found_entry, current_entry, current_entry->size);
      break;
    }
    kfree(dname);
  }

  kfree(blk_buf);

  if (found_entry == NULL) { kunlock(&(self->ops_lock)); return NULL; }

  fs_node_t *outnode = kmalloc(sizeof(fs_node_t));
  CHECK_UNLOCK_O(outnode == NULL, "No memory.", NULL);
  u_memset(outnode, 0, sizeof(fs_node_t));
  res = read_inode_info(self, &inode, found_entry->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", NULL);
  make_ext2_node(self, outnode, &inode, found_entry);

  kfree(found_entry);
  kunlock(&(self->ops_lock));
  return outnode;
}

static uint32_t ext2_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", 0);
  if (inode.size == 0) { kunlock(&(self->ops_lock)); return 0; }

  uint32_t end = offset + size;
  if (end > inode.size) end = inode.size;

  uint32_t start_block = offset / self->block_size;
  uint32_t start_offset = offset % self->block_size;
  uint32_t end_block = end / self->block_size;
  uint32_t end_post_offset = end - (end_block * self->block_size);
  uint32_t size_to_read = end - offset;

  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(blk_buf == NULL, "No memory.", 0);
  if (start_block == end_block) {
    res = read_inode_block(self, &inode, start_block, blk_buf);
    CHECK_UNLOCK_O(res != self->block_size, "Failed to read block.", res);
    u_memcpy(buffer, blk_buf + start_offset, size_to_read);

    kfree(blk_buf);
    kunlock(&(self->ops_lock));
    return size_to_read;
  }

  uint32_t current_block = start_block;
  uint32_t read_blocks = 0;
  for (; current_block < end_block; ++current_block, ++read_blocks) {
    res = read_inode_block(self, &inode, current_block, blk_buf);
    CHECK_UNLOCK_O(
      res != self->block_size,
      "Failed to read block.",
      (read_blocks * self->block_size) + res
      );
    if (current_block == start_block) {
      u_memcpy(
        buffer, blk_buf + start_offset, self->block_size - start_offset
        );
      continue;
    }
    u_memcpy(
      buffer + (read_blocks * self->block_size) - start_offset,
      blk_buf,
      self->block_size
      );
  }

  if (end_post_offset) {
    res = read_inode_block(self, &inode, end_block, blk_buf);
    CHECK_UNLOCK_O(
      res != self->block_size,
      "Failed to read block.",
      (read_blocks * self->block_size) + res
      );
    u_memcpy(
      buffer + (read_blocks * self->block_size) - start_offset,
      blk_buf,
      end_post_offset
      );
  }

  kfree(blk_buf);
  kunlock(&(self->ops_lock));
  return size_to_read;
}

static uint32_t ext2_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", 0);

  uint32_t end = offset + size;
  if (end > inode.size) {
    inode.size = end;
    res = write_inode_info(self, &inode, node->inode);
    CHECK_UNLOCK_O(res, "Failed to write inode info.", 0);
  }

  uint32_t start_block = offset / self->block_size;
  uint32_t start_offset = offset % self->block_size;
  uint32_t end_block = end / self->block_size;
  uint32_t end_post_offset = end - (end_block * self->block_size);

  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(blk_buf == NULL, "No memory.", 0);
  if (start_block == end_block) {
    res = read_inode_block(self, &inode, start_block, blk_buf);
    CHECK_UNLOCK_O(res != self->block_size, "Failed to read inode block.", 0);
    u_memcpy(blk_buf + start_offset, buffer, size);
    res = write_inode_block(self, &inode, node->inode, start_block, blk_buf);
    CHECK_UNLOCK_O(res != self->block_size, "Failed to write inode block.", res);

    kfree(blk_buf);
    kunlock(&(self->ops_lock));
    return size;
  }

  uint32_t current_block = start_block;
  uint32_t written_blocks = 0;
  for (; current_block < end_block; ++current_block, ++written_blocks) {
    uint32_t written_size = written_blocks * self->block_size;
    read_inode_block(self, &inode, current_block, blk_buf);
    if (current_block == start_block) {
      u_memcpy(
        blk_buf + start_offset, buffer, self->block_size - start_offset
        );
      res = write_inode_block(
        self, &inode, node->inode, current_block, blk_buf
        );
      CHECK_UNLOCK_O(res != self->block_size, "Failed to write inode block", res);
      res = read_inode_info(self, &inode, node->inode);
      CHECK_UNLOCK_O(
        res, "Failed to read inode info.", self->block_size - start_offset
        );
      continue;
    }

    u_memcpy(
      blk_buf, buffer + written_size - start_offset, self->block_size
      );
    res = write_inode_block(self, &inode, node->inode, current_block, blk_buf);
    CHECK_UNLOCK_O(
      res != self->block_size,
      "Failed to write inode block.",
      res + written_size
      );
    res = read_inode_info(self, &inode, node->inode);
    CHECK_UNLOCK_O(
      res, "Failed to read inode info.", written_size + self->block_size
      );
  }

  if (end_post_offset) {
    res = read_inode_block(self, &inode, end_block, blk_buf);
    CHECK_UNLOCK_O(
      res != self->block_size,
      "Failed to read inode block.",
      (written_blocks * self->block_size) + res
      );
    u_memcpy(
      blk_buf,
      buffer + (written_blocks * self->block_size) - start_offset,
      end_post_offset
      );
    res = write_inode_block(self, &inode, node->inode, end_block, blk_buf);
    CHECK_UNLOCK_O(
      res != self->block_size,
      "Failed to write inode block.",
      (written_blocks * self->block_size) + res
      );
  }

  kfree(blk_buf);
  kunlock(&(self->ops_lock));
  return 0;
}

static void ext2_open(fs_node_t *node, uint32_t flags)
{
  if ((flags & O_TRUNC) == 0) return;

  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  if (res) {
    log_error("ext2", "Failed to read inode.\n");
    kunlock(&(self->ops_lock)); return;
  }
  inode.size = 0;
  res = write_inode_info(self, &inode, node->inode);
  kunlock(&(self->ops_lock));
  if (res) { log_error("ext2", "Failed to write inode.\n"); return; }
}

static void ext2_close(fs_node_t *node)
{}

static int32_t ext2_mkdir(fs_node_t *node, char *name, uint16_t mask)
{
  ext2_fs_t *self = node->device;
  fs_node_t *child = fs_finddir(node, name);
  kfree(child);
  if (child) return -EEXIST;

  klock(&(self->ops_lock));

  uint32_t inode_num = alloc_inode(self);
  if (inode_num == 0) { kunlock(&(self->ops_lock)); return -ENOSPC; }
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);

  // TODO atime, mtime, ctime
  inode.atime = 0;
  inode.ctime = 0;
  inode.mtime = 0;

  u_memset(&(inode.block_pointer), 0, sizeof(inode.block_pointer));
  inode.sector_count = 0;
  inode.size = 0;

  // TODO users.
  inode.uid = 0;
  inode.gid = 0;

  inode.fragment_addr = 0;
  inode.hard_link_count = 2;
  inode.flags = 0;
  inode.os_1 = 0;
  inode.generation_number = 0;
  inode.file_acl = 0;
  inode.dir_acl = 0;
  inode.permissions = EXT2_S_IFDIR;
  inode.permissions |= 0xFFF & mask;

  res = write_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);
  int32_t sres = create_dir_entry(node, name, inode_num);
  CHECK_UNLOCK_O(sres < 0, "Failed to create directory entry.", res);
  inode.size = self->block_size;
  res = write_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);

  uint8_t *buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(buf == NULL, "No memory.", -ENOMEM);
  ext2_dir_entry_t *ent = kmalloc(12);
  CHECK_UNLOCK_O(ent == NULL, "No memory.", -ENOMEM);
  u_memset(ent, 0, 12);
  ent->inode = inode_num;
  ent->size = 12;
  ent->name_len = 1;
  ent->name[0] = '.';
  u_memcpy(buf, ent, 12);
  ent->inode = node->inode;
  ent->name_len = 2;
  ent->name[1] = '.';
  ent->size = self->block_size - 12;
  u_memcpy(&buf[12], ent, 12);
  res = write_inode_block(self, &inode, inode_num, 0, buf);
  CHECK_UNLOCK_O(res != self->block_size, "Failed to write inode block.", -EAGAIN);
  kfree(ent);
  kfree(buf);

  ext2_inode_t parent_inode;
  res = read_inode_info(self, &parent_inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);
  ++(parent_inode.hard_link_count);
  res = write_inode_info(self, &parent_inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);

  uint32_t group_num = inode_num / self->inodes_per_group;
  ++(self->bgds[group_num].dir_count);
  res = write_bgds(self);
  CHECK_UNLOCK_O(res, "Failed to write block group descriptors.", -res);

  kunlock(&(self->ops_lock));
  return 0;
}

static int32_t ext2_create(fs_node_t *node, char *name, uint16_t mask)
{
  ext2_fs_t *self = node->device;
  fs_node_t *child = fs_finddir(node, name);
  kfree(child);
  if (child) return -EEXIST;

  klock(&(self->ops_lock));

  uint32_t inode_num = alloc_inode(self);
  if (inode_num == 0) { kunlock(&(self->ops_lock)); return -ENOSPC; }
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);

  // TODO atime, mtime, ctime
  inode.atime = 0;
  inode.ctime = 0;
  inode.mtime = 0;

  u_memset(&(inode.block_pointer), 0, sizeof(inode.block_pointer));
  inode.sector_count = 0;
  inode.size = 0;

  // TODO users
  inode.uid = 0;
  inode.gid = 0;

  inode.fragment_addr = 0;
  inode.hard_link_count = 1;
  inode.flags = 0;
  inode.os_1 = 0;
  inode.generation_number = 0;
  inode.file_acl = 0;
  inode.dir_acl = 0;
  inode.permissions = EXT2_S_IFREG;
  inode.permissions |= 0xFFF & mask;

  res = write_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);
  int32_t sres = create_dir_entry(node, name, inode_num);
  CHECK_UNLOCK_O(sres < 0, "Failed to create directory entry.", sres);

  kunlock(&(self->ops_lock));
  return 0;
}

static int32_t ext2_unlink(fs_node_t *node, char *name)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);
  if ((inode.permissions & EXT2_S_IFDIR) == 0) {
    kunlock(&(self->ops_lock)); return -ENOTDIR;
  }

  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(blk_buf == NULL, "No memory.", -ENOMEM);
  uint32_t block_num = 0;
  res = read_inode_block(self, &inode, block_num, blk_buf);
  CHECK_UNLOCK_O(res != self->block_size, "Failed to read inode block.", -EAGAIN);

  uint32_t idx = 0;
  uint32_t dir_idx = 0;
  ext2_dir_entry_t *current_entry = NULL;
  ext2_dir_entry_t *found_entry = NULL;
  for (
    ;
    idx < inode.size;
    idx += current_entry->size, dir_idx += current_entry->size
    )
  {
    if (dir_idx >= self->block_size) {
      ++block_num;
      dir_idx -= self->block_size;
      res = read_inode_block(self, &inode, block_num, blk_buf);
      CHECK_UNLOCK_O(
        res != self->block_size, "Failed to read inode block.", -EAGAIN
        );
    }

    current_entry = (ext2_dir_entry_t *)(blk_buf + dir_idx);
    if (current_entry->inode == 0 || u_strlen(name) != current_entry->name_len)
      continue;

    char *dname = kmalloc(current_entry->name_len + 1);
    CHECK_UNLOCK_O(dname == NULL, "No memory.", -ENOMEM);
    u_memcpy(dname, current_entry->name, current_entry->name_len);
    dname[current_entry->name_len] = '\0';
    if (u_strcmp(dname, name) == 0) {
      kfree(dname);
      found_entry = current_entry;
      break;
    }
    kfree(dname);
  }

  if (found_entry == NULL) {
    kfree(blk_buf); kunlock(&self->ops_lock); return -ENOENT;
  }

  uint32_t child_inode_num = found_entry->inode;
  ext2_inode_t child_inode;
  res = read_inode_info(self, &child_inode, child_inode_num);
  CHECK_UNLOCK_O(res, "Failed to read child inode info.", -res);

  uint8_t isfile = (child_inode.permissions & EXT2_S_IFREG) == EXT2_S_IFREG;
  uint8_t islink = (child_inode.permissions & EXT2_S_IFLNK) == EXT2_S_IFLNK;
  uint8_t isdir = (child_inode.permissions & EXT2_S_IFDIR) == EXT2_S_IFDIR;

  if (isdir)
    if (ext2_readdir_inode(self, &child_inode, 2)) {
      kfree(blk_buf); kunlock(&(self->ops_lock)); return -EPERM;
    }

  found_entry->inode = 0;
  res = write_inode_block(self, &inode, node->inode, block_num, blk_buf);
  CHECK_UNLOCK_O(res != self->block_size, "Failed to write inode block.", -EAGAIN);
  --(child_inode.hard_link_count);
  res = write_inode_info(self, &child_inode, child_inode_num);
  CHECK_UNLOCK_O(res, "Failed to write child inode info.", -res);

  if (isdir) {
    --(inode.hard_link_count);
    res = write_inode_info(self, &inode, node->inode);
    CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);
  }

  // TODO actually make freeing space work.
  /* if ( */
  /*   ((islink || isfile) && child_inode.hard_link_count == 0) */
  /*   || (isdir && child_inode.hard_link_count == 1) */
  /*   ) */
  /* { */
  /*   uint32_t tmp = child_inode.sector_count / (self->block_size / 512); */
  /*   for (uint32_t i = 0; i < tmp; ++i) { */
  /*     res = free_inode_block(self, &child_inode, child_inode_num, i); */
  /*     CHECK(res, "Failed to free inode block.", -res); */
  /*     res = read_inode_info(self, &child_inode, child_inode_num); */
  /*     CHECK(res, "Failed to read inode info.", -res); */
  /*   } */

  /*   res = free_inode(self, child_inode_num); */
  /*   CHECK(res, "Failed to free inode.", -res); */
  /* } */

  kfree(blk_buf);
  kunlock(&(self->ops_lock));
  return 0;
}

static int32_t ext2_chmod(fs_node_t *node, int32_t mask)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);
  inode.permissions = (inode.permissions & 0xFFFFF000) | mask;
  res = write_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);
  kunlock(&(self->ops_lock));
  return 0;
}

static int32_t ext2_symlink(fs_node_t *node, char *value, char *name)
{
  ext2_fs_t *self = node->device;
  fs_node_t *child = fs_finddir(node, name);
  kfree(child);
  if (child) return -EEXIST;

  klock(&(self->ops_lock));

  uint32_t inode_num = alloc_inode(self);
  if (inode_num == 0) { kunlock(&(self->ops_lock)); return -ENOSPC; }
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);

  // TODO atime, mtime, ctime
  inode.atime = 0;
  inode.ctime = 0;
  inode.mtime = 0;

  u_memset(&(inode.block_pointer), 0, sizeof(inode.block_pointer));
  inode.sector_count = 0;
  inode.size = 0;

  // TODO users
  inode.uid = 0;
  inode.gid = 0;

  inode.fragment_addr = 0;
  inode.hard_link_count = 1;
  inode.flags = 0;
  inode.os_1 = 0;
  inode.generation_number = 0;
  inode.file_acl = 0;
  inode.dir_acl = 0;
  inode.permissions = EXT2_S_IFLNK;
  inode.permissions |= 0660;

  uint32_t src_len = u_strlen(value);
  uint8_t embedded = src_len <= sizeof(inode.block_pointer);
  if (embedded) {
    u_memcpy(&(inode.block_pointer), value, src_len);
    inode.size = src_len;
  }

  res = write_inode_info(self, &inode, inode_num);
  CHECK_UNLOCK_O(res, "Failed to write inode info.", -res);
  int32_t sres = create_dir_entry(node, name, inode_num);
  CHECK_UNLOCK_O(sres < 0, "Failed to create directory entry.", sres);

  kunlock(&(self->ops_lock));

  if (!embedded) {
    fs_node_t tmp;
    tmp.device = self;
    tmp.inode = inode_num;
    res = ext2_write(&tmp, 0, src_len, (uint8_t *)value);
    CHECK(res != src_len, "Failed to write symlink.", -EAGAIN);
  }

  return 0;
}

static int32_t ext2_readlink(fs_node_t *node, char *buf, size_t bufsize)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);
  uint32_t size = bufsize;
  if (inode.size < bufsize) size = inode.size;

  kunlock(&(self->ops_lock));

  uint32_t read_size = 0;
  if (inode.size > sizeof(inode.block_pointer))
    read_size = ext2_read(node, 0, size, (uint8_t *)buf);
  else {
    u_memcpy(buf, &(inode.block_pointer), size);
    read_size = size;
  }

  if (size < bufsize) buf[size] = '\0';

  return read_size;
}

static int32_t ext2_rename(fs_node_t *node, char *old, char *new)
{
  ext2_fs_t *self = node->device;
  klock(&(self->ops_lock));
  ext2_inode_t inode;
  uint32_t res = read_inode_info(self, &inode, node->inode);
  CHECK_UNLOCK_O(res, "Failed to read inode info.", -res);

  uint8_t *blk_buf = kmalloc(self->block_size);
  CHECK_UNLOCK_O(blk_buf == NULL, "No memory.", -ENOMEM);
  uint32_t block_num = 0;
  res = read_inode_block(self, &inode, block_num, blk_buf);
  CHECK_UNLOCK_O(res != self->block_size, "Failed to read inode block.", -EAGAIN);

  uint32_t idx = 0;
  uint32_t dir_idx = 0;
  ext2_dir_entry_t *current_entry = NULL;
  ext2_dir_entry_t *old_entry = NULL;
  ext2_dir_entry_t *new_entry = NULL;
  for (
    ;
    idx < inode.size;
    idx += current_entry->size, dir_idx += current_entry->size
    )
  {
    if (dir_idx >= self->block_size) {
      ++block_num;
      dir_idx -= self->block_size;
      res = read_inode_block(self, &inode, block_num, blk_buf);
      CHECK_UNLOCK_O(
        res != self->block_size, "Failed to read inode block.", -EAGAIN
        );
    }

    current_entry = (ext2_dir_entry_t *)(blk_buf + dir_idx);
    if (current_entry->inode == 0)
      continue;

    char *dname = kmalloc(current_entry->name_len + 1);
    CHECK_UNLOCK_O(dname == NULL, "No memory.", -ENOMEM);
    u_memcpy(dname, current_entry->name, current_entry->name_len);
    dname[current_entry->name_len] = '\0';
    if (u_strcmp(dname, old) == 0) old_entry = current_entry;
    if (u_strcmp(dname, new) == 0) new_entry = current_entry;
    kfree(dname);
  }

  if (old_entry == NULL) {
    kunlock(&(self->ops_lock)); kfree(blk_buf); return -ENOENT;
  }
  if (new_entry) {
    // On most systems, `rename' removes the directory entry with the new
    // name.
    // I think that's a bad design choice and I also don't want to
    // implement it, so I won't.
    kunlock(&(self->ops_lock)); kfree(blk_buf); return -EEXIST;
  }

  uint32_t child_inode_num = old_entry->inode;
  old_entry->inode = 0;
  res = write_inode_block(self, &inode, node->inode, block_num, blk_buf);
  CHECK_UNLOCK_O(
    res != self->block_size, "Failed to write inode block.", -EAGAIN
    );

  int32_t sres = create_dir_entry(node, new, child_inode_num);
  CHECK_UNLOCK_O(sres < 0, "Failed to create directory entry.", sres);

  kunlock(&(self->ops_lock));
  return 0;
}

static void make_ext2_node(
  ext2_fs_t *self, fs_node_t *node, ext2_inode_t *inode, ext2_dir_entry_t *ent
  )
{
  u_memset(node, 0, sizeof(fs_node_t));
  node->device = self;
  node->inode = ent->inode;
  u_memcpy(node->name, ent->name, ent->name_len);
  node->name[ent->name_len] = '\0';
  node->uid = inode->uid;
  node->gid = inode->gid;
  node->length = inode->size;
  node->mask = inode->permissions;
  node->atime = inode->atime;
  node->mtime = inode->mtime;
  node->ctime = inode->ctime;
  node->open = ext2_open;
  node->close = ext2_close;
  node->chmod = ext2_chmod;
  node->rename = ext2_rename;
  if ((inode->permissions & EXT2_S_IFREG) == EXT2_S_IFREG) {
    node->flags |= FS_FILE;
    node->read = ext2_read;
    node->write = ext2_write;
  }
  if ((inode->permissions & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
    node->flags |= FS_DIRECTORY;
    node->readdir = ext2_readdir;
    node->finddir = ext2_finddir;
    node->create = ext2_create;
    node->mkdir = ext2_mkdir;
    node->unlink = ext2_unlink;
    node->symlink = ext2_symlink;
  }
  if ((inode->permissions & EXT2_S_IFBLK) == EXT2_S_IFBLK)
    node->flags |= FS_BLOCKDEVICE;
  if ((inode->permissions & EXT2_S_IFCHR) == EXT2_S_IFCHR)
    node->flags |= FS_CHARDEVICE;
  if ((inode->permissions & EXT2_S_IFIFO) == EXT2_S_IFIFO)
    node->flags |= FS_PIPE;
  if ((inode->permissions & EXT2_S_IFLNK) == EXT2_S_IFLNK) {
    node->flags |= FS_SYMLINK;
    node->readlink = ext2_readlink;
  }
}

static uint32_t make_fs_root(
  ext2_fs_t *self, ext2_inode_t *inode, fs_node_t *node
  )
{
  node->device = self;
  u_memcpy(node->name, EXT2_ROOT, u_strlen(EXT2_ROOT) + 1);
  node->inode = 2;
  node->length = inode->size;
  node->mask = inode->permissions;
  node->uid = inode->uid;
  node->gid = inode->gid;
  node->flags |= FS_DIRECTORY;
  node->atime = inode->atime;
  node->mtime = inode->mtime;
  node->ctime = inode->ctime;
  node->read = NULL;
  node->write = NULL;
  node->readdir = ext2_readdir;
  node->finddir = ext2_finddir;
  node->open = ext2_open;
  node->close = ext2_close;
  node->mkdir = ext2_mkdir;
  node->create = ext2_create;
  node->chmod = ext2_chmod;
  node->unlink = ext2_unlink;
  node->symlink = ext2_symlink;
  node->readlink = ext2_readlink;
  node->rename = ext2_rename;

  return 0;
}

uint32_t ext2_init(const char *dev_path)
{
  ext2_fs_t *e2fs = kmalloc(sizeof(ext2_fs_t));
  CHECK(e2fs == NULL, "No memory.", ENOMEM);
  u_memset(e2fs, 0, sizeof(ext2_fs_t));

  e2fs->block_device = kmalloc(sizeof(fs_node_t));
  CHECK(e2fs->block_device == NULL, "No memory.", ENOMEM);
  uint32_t res = fs_open_node(e2fs->block_device, dev_path, 0);
  CHECK(res, "Failed to open block device.", res);

  e2fs->block_size = 1024;
  e2fs->superblock = kmalloc(e2fs->block_size);
  CHECK(e2fs->superblock == NULL, "No memory.", ENOMEM);
  res = read_block(e2fs, 1, (uint8_t *)e2fs->superblock);
  CHECK(res != e2fs->block_size, "Unable to read superblock.", res);
  CHECK(
    e2fs->superblock->ext2_magic != EXT2_MAGIC,
    "Not an ext2 filesystem.",
    1
    );
  e2fs->block_size = 1024 << e2fs->superblock->block_size_offset;
  e2fs->blocks_per_group = e2fs->superblock->blocks_per_group;
  e2fs->inodes_per_group = e2fs->superblock->inodes_per_group;
  e2fs->group_count = e2fs->superblock->block_count / e2fs->blocks_per_group;
  if (
    e2fs->group_count * e2fs->blocks_per_group
    < e2fs->superblock->block_count
    ) ++(e2fs->group_count);

  e2fs->bgd_block_count = (e2fs->group_count * sizeof(ext2_bgd_t))
    / e2fs->block_size;
  if (
    e2fs->bgd_block_count * e2fs->block_size
    < e2fs->group_count * sizeof(ext2_bgd_t)
    ) ++(e2fs->bgd_block_count);
  e2fs->bgds = kmalloc(e2fs->block_size * e2fs->bgd_block_count);
  CHECK(e2fs->bgds == NULL, "No memory.", ENOMEM);
  uint32_t bgd_block_offset = e2fs->block_size > 1024 ? 1 : 2;
  for (uint32_t i = 0; i < e2fs->bgd_block_count; ++i) {
    res = read_block(
      e2fs,
      bgd_block_offset + i,
      (uint8_t *)((uint32_t)e2fs->bgds + (i * e2fs->block_size))
      );
    CHECK(
      res != e2fs->block_size, "Unable to read block group descriptors.", res
      );
  }

  ext2_inode_t root_inode;
  res = read_inode_info(e2fs, &root_inode, 2);
  CHECK(res, "Failed to read root inode.", res);
  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  CHECK(node == NULL, "No memory.", ENOMEM);
  u_memset(node, 0, sizeof(fs_node_t));
  make_fs_root(e2fs, &root_inode, node);
  fs_mount(node, EXT2_ROOT);

  return 0;
}
