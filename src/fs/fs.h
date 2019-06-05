
// fs.h
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _FS_H_
#define _FS_H_

#include <stdint.h>

// The maximum length of a file name.
#define FS_NAME_LEN 256

// fs_node_t flags.
const uint32_t FS_FILE        = 1;
const uint32_t FS_DIRECTORY   = 2;
const uint32_t FS_CHARDEVICE  = 3;
const uint32_t FS_BLOCKDEVICE = 4;
const uint32_t FS_PIPE        = 5;
const uint32_t FS_SYMLINK     = 6;
const uint32_t FS_MOUNTPOINT  = 8;

struct fs_node_s;
struct dirent;

// File operations: open, close, etc.
typedef void (*open_type_t)(struct fs_node_s *, uint32_t);
typedef void (*close_type_t)(struct fs_node_s *);
typedef uint32_t (*read_type_t)(
  struct fs_node_s *, uint32_t, uint32_t, uint8_t *
  );
typedef uint32_t (*write_type_t)(
  struct fs_node_s *, uint32_t, uint32_t, uint8_t *
  );
typedef struct dirent *(*readdir_type_t)(struct fs_node_s *, uint32_t);
typedef struct fs_node_s *(*finddir_type_t)(struct fs_node_s *, char *);

// A single filesystem node.
typedef struct fs_node_s {
  char name[FS_NAME_LEN]; // File name.
  uint32_t mask;          // Permissions mask (rwx).
  uint32_t uid;           // User ID.
  uint32_t gid;           // Group ID.
  uint32_t flags;         // Flags (see above).
  uint32_t inode;         // Inode number.
  uint32_t length;        // File size in bytes.
  uint32_t impl;          // TODO

  // File operations.
  open_type_t open;
  close_type_t close;
  read_type_t read;
  write_type_t write;
  readdir_type_t readdir;
  finddir_type_t finddir;

  struct fs_node_s *ptr; // Pointer for symlinks.
} fs_node_t;

// A single directory entry.
struct dirent {
  char name[FS_NAME_LEN];
  uint32_t ino;
};

void open_fs(fs_node_t *, uint32_t);
void close_fs(fs_node_t *);
uint32_t read_fs(fs_node_t *, uint32_t, uint32_t, uint8_t *);
uint32_t write_fs(fs_node_t *, uint32_t, uint32_t, uint8_t *);
struct dirent *readdir_fs(fs_node_t *, uint32_t);
fs_node_t *finddir_fs(fs_node_t *, char *);

#endif /* _FS_H_ */
