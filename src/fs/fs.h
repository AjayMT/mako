
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

// Path constants.
#define FS_DIR_SELF     "."
#define FS_DIR_UP       ".."
#define FS_PATH_SEP     '/'
#define FS_PATH_SEP_STR "/"

// fs_node_t flags.
#define FS_FILE        1
#define FS_DIRECTORY   2
#define FS_CHARDEVICE  3
#define FS_BLOCKDEVICE 4
#define FS_PIPE        5
#define FS_SYMLINK     6
#define FS_MOUNTPOINT  8

// fs_open flags.
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_APPEND    8
#define O_CREAT     0x200
#define O_TRUNC     0x400
#define O_EXCL      0x800
#define O_NOFOLLOW  0x1000
#define O_PATH      0x2000
#define O_NONBLOCK  0x4000
#define O_DIRECTORY 0x8000

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
  uint32_t impl;          // Used to track which FS it is part of.
  void *device;           // Device object.

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

// VFS entry.
typedef struct vfs_entry_s {
  char name[FS_NAME_LEN];
  fs_node_t *file;
} vfs_entry_t;

// Interface for all filesystems.
void fs_open(fs_node_t *, uint32_t);
void fs_close(fs_node_t *);
uint32_t fs_read(fs_node_t *, uint32_t, uint32_t, uint8_t *);
uint32_t fs_write(fs_node_t *, uint32_t, uint32_t, uint8_t *);
struct dirent *fs_readdir(fs_node_t *, uint32_t);
fs_node_t *fs_finddir(fs_node_t *, char *);

// Initialize the filesystem interface.
uint32_t fs_init();

// Mount a filesystem.
uint32_t fs_mount(fs_node_t *, const char *);

// Open the filesystem node at a path.
uint32_t fs_open_node(fs_node_t *, const char *, uint32_t);

#endif /* _FS_H_ */
