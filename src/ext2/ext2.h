
// ext2.h
//
// Ext2 filesystem implementation.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _EXT2_H_
#define _EXT2_H_

#include <stdint.h>
#include <fs/fs.h>

#define EXT2_ROOT "/ext2"

struct ext2_superblock_s {
  uint32_t inode_count;
  uint32_t block_count;
  uint32_t root_block_count;
  uint32_t free_block_count;
  uint32_t free_inode_count;
  uint32_t superblock_idx;
  uint32_t block_size_offset;
  uint32_t fragment_size_offset;
  uint32_t blocks_per_group;
  uint32_t fragments_per_group;
  uint32_t inodes_per_group;
  uint32_t mount_time;
  uint32_t write_time;
  uint16_t mount_count;
  uint16_t mount_count_allowed;
  uint16_t ext2_magic;
  uint16_t fs_state;
  uint16_t err;
  uint16_t version_minor;
  uint32_t last_check;
  uint32_t check_interval;
  uint32_t os_id;
  uint32_t version_major;
  uint16_t resv_uid;
  uint16_t resv_gid;

  // Extended features.
  uint32_t first_inode;
  uint16_t inode_size;
  uint16_t superblock_group;
  uint32_t optional_features;
  uint16_t required_features;
  uint32_t readonly_features;
  uint8_t fs_id[16];
  uint8_t volume_name[16];
  uint8_t last_mount_path[64];
  uint32_t compression_algorithm;
  uint8_t file_preallocated_blocks;
  uint8_t dir_preallocated_blocks;
  uint16_t unused0;
  uint8_t journal_id[16];
  uint32_t journal_inode;
  uint32_t journal_device;
  uint32_t orphan_head;
  uint8_t unused1[788];
} __attribute__((packed));
typedef struct ext2_superblock_s ext2_superblock_t;

struct ext2_bgd_s {
  uint32_t block_bitmap;
  uint32_t inode_bitmap;
  uint32_t inode_table;
  uint32_t free_block_count;
  uint32_t free_inode_count;
  uint32_t dir_count;
  uint8_t unused[14];
} __attribute__((packed));
typedef struct ext2_bgd_s ext2_bgd_t;

struct ext2_inode_s {
  uint16_t permissions;
  uint16_t uid;
  uint32_t size;
  uint32_t atime;
  uint32_t ctime;
  uint32_t mtime;
  uint32_t dtime;
  uint16_t gid;
  uint16_t hard_link_count;
  uint32_t sector_count;
  uint32_t flags;
  uint32_t os_1;
  uint32_t block_pointer[15];
  uint32_t generation_number;
  uint32_t file_acl;
  union {
    uint32_t dir_acl;
    uint32_t size_high;
  };
  uint32_t fragment_addr;
  uint8_t os_2[12];
} __attribute__((packed));
typedef struct ext2_inode_s ext2_inode_t;

#define EXT2_S_IFSOCK 0xC000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFBLK  0x6000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFCHR  0x2000
#define EXT2_S_IFIFO  0x1000

#define EXT2_S_ISUID 0x0800
#define EXT2_S_ISGID 0x0400
#define EXT2_S_ISVTX 0x0200

#define EXT2_S_IRUSR 0x0100
#define EXT2_S_IWUSR 0x0080
#define EXT2_S_IXUSR 0x0040
#define EXT2_S_IRGRP 0x0020
#define EXT2_S_IWGRP 0x0010
#define EXT2_S_IXGRP 0x0008
#define EXT2_S_IROTH 0x0004
#define EXT2_S_IWOTH 0x0002
#define EXT2_S_IXOTH 0x0001

struct ext2_dir_entry_s {
  uint32_t inode;
  uint16_t size;
  uint8_t name_len;
  uint8_t type;
  uint8_t name[];
} __attribute__((packed));
typedef struct ext2_dir_entry_s ext2_dir_entry_t;

uint32_t ext2_init(const char *);

#endif /* _EXT2_H_ */
