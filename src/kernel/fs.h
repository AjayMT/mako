
// fs.h
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _FS_H_
#define _FS_H_

#include "../common/stdint.h"
#include "../libc/dirent.h"
#include "ds.h"
#include <stddef.h>

#define FS_NAME_LEN 256
#define FS_DIR_SELF "."
#define FS_DIR_UP ".."
#define FS_PATH_SEP '/'

enum fs_node_type
{
  FS_FILE = 0,
  FS_DIRECTORY = 1,
  FS_SYMLINK = 2,
  FS_BLOCKDEVICE = 3,
  FS_PIPE = 4,
};

// fs_open flags.
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 8
#define O_CREAT 0x200
#define O_TRUNC 0x400
#define O_EXCL 0x800
#define O_NOFOLLOW 0x1000
#define O_PATH 0x2000
#define O_NONBLOCK 0x4000
#define O_DIRECTORY 0x8000

struct fs_node_s;

// File operations: open, close, etc.
typedef void (*open_type_t)(struct fs_node_s *, uint32_t);
typedef void (*close_type_t)(struct fs_node_s *);
typedef uint32_t (*read_type_t)(struct fs_node_s *, uint32_t, uint32_t, uint8_t *);
typedef uint32_t (*write_type_t)(struct fs_node_s *, uint32_t, uint32_t, uint8_t *);
typedef struct dirent *(*readdir_type_t)(struct fs_node_s *, uint32_t);
typedef struct fs_node_s *(*finddir_type_t)(struct fs_node_s *, char *);
typedef int32_t (*mkdir_type_t)(struct fs_node_s *, char *, uint16_t);
typedef int32_t (*create_type_t)(struct fs_node_s *, char *, uint16_t);
typedef int32_t (*unlink_type_t)(struct fs_node_s *, char *);
typedef int32_t (*chmod_type_t)(struct fs_node_s *, int32_t);
typedef int32_t (*symlink_type_t)(struct fs_node_s *, char *, char *);
typedef int32_t (*readlink_type_t)(struct fs_node_s *, char *, size_t);
typedef int32_t (*rename_type_t)(struct fs_node_s *, char *, char *);

// A single filesystem node.
typedef struct fs_node_s
{
  char name[FS_NAME_LEN]; // File name.
  uint32_t size;          // File size in bytes.
  enum fs_node_type type; // Node type (see above).
  uint32_t inode;         // Inode number; de-facto the offset in the underlying block device.
  void *device;           // Implementation-defined pointer to a block device / other object.
  tree_node_t *tree_node; // Mount point tree node; non-NULL if and only
                          // if this node is a mount point.

  uint32_t mask;  // Permissions mask (rwx).
  uint32_t uid;   // User ID.
  uint32_t gid;   // Group ID.
  uint32_t atime; // Accessed time.
  uint32_t ctime; // Created time.
  uint32_t mtime; // Modified time.

  // File operations.
  open_type_t open;
  close_type_t close;
  read_type_t read;
  write_type_t write;
  readdir_type_t readdir;
  finddir_type_t finddir;
  mkdir_type_t mkdir;
  create_type_t create;
  chmod_type_t chmod;
  unlink_type_t unlink;
  symlink_type_t symlink;
  readlink_type_t readlink;
  rename_type_t rename;
} fs_node_t;

// Filesystem interface {
// These are simple wrappers around the corresponding fs_node_t functions.
void fs_open(fs_node_t *, uint32_t);
void fs_close(fs_node_t *);
int32_t fs_read(fs_node_t *, uint32_t, uint32_t, uint8_t *);
int32_t fs_write(fs_node_t *, uint32_t, uint32_t, uint8_t *);
struct dirent *fs_readdir(fs_node_t *, uint32_t);
fs_node_t *fs_finddir(fs_node_t *, char *);
int32_t fs_chmod(fs_node_t *, int32_t);
int32_t fs_readlink(fs_node_t *, char *, size_t);

// These are non-trivial operations that use fs_node_t functions.
int32_t fs_symlink(char *, char *);
int32_t fs_mkdir(char *, uint16_t);
int32_t fs_create(char *, uint16_t);
int32_t fs_unlink(char *);
int32_t fs_rename(char *, char *);
// }

// Initialize the filesystem interface and VFS.
uint32_t fs_init();

// Mount a filesystem.
uint32_t fs_mount(fs_node_t *, const char *);

// Open the filesystem node at a path.
uint32_t fs_open_node(fs_node_t *, const char *, uint32_t);

// Resolve a (relative) path.
uint32_t fs_resolve_path(char **outpath, const char *inpath);

#endif /* _FS_H_ */
