
// fs.c
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <kheap/kheap.h>
#include <ds/ds.h>
#include <util/util.h>
#include <debug/log.h>
#include "fs.h"

const uint32_t FS_FLAGS_MASK = 7;

// TODO return/set error codes.

// Filesystem mountpoint tree.
static tree_node_t *fs_tree = NULL;

void fs_open(fs_node_t *node, uint32_t flags)
{ if (node && node->open) node->open(node, flags); }
void fs_close(fs_node_t *node)
{ if (node && node->close) node->close(node); }
uint32_t fs_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (node && node->read)
    return node->read(node, offset, size, buffer);
  return 0;
}
uint32_t fs_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (node && node->read)
    return node->read(node, offset, size, buffer);
  return 0;
}
struct dirent *fs_readdir(fs_node_t *node, uint32_t index)
{
  if (node && (node->flags & FS_FLAGS_MASK) == FS_DIRECTORY && node->readdir)
    return node->readdir(node, index);
  return NULL;
}
fs_node_t *fs_finddir(fs_node_t *node, char *name)
{
  if (node && (node->flags & FS_FLAGS_MASK) == FS_DIRECTORY && node->finddir)
    return node->finddir(node, name);
  return NULL;
}

// Initialize the filesystem interface.
void fs_init()
{
  vfs_entry_t *vfs_root = kmalloc(sizeof(vfs_entry_t));
  u_memset(vfs_root, 0, sizeof(vfs_entry_t));
  fs_tree = tree_init(vfs_root);
}

// Mount a filesystem at a path.
void fs_mount(fs_node_t *local_root, const char *path)
{
  if (fs_tree == NULL) return;

  size_t path_len = u_strlen(path);
  char *mpath = kmalloc(path_len + 1);
  u_memcpy(mpath, path, path_len + 1);

  if (path[0] != FS_PATH_SEP) {
    log_debug("fs", "Mount path %s must be absolute.", path);
    return;
  }

  for (size_t i = 0; i < path_len; ++i)
    if (mpath[i] == FS_PATH_SEP) mpath[i] = '\0';

  tree_node_t *node = fs_tree;
  for (
    size_t path_idx = 1;
    path_idx < path_len;
    path_idx += u_strlen(mpath + path_idx) + 1
    )
  {
    uint8_t found = 0;
    list_foreach(lchild, node->children) {
      tree_node_t *child = (tree_node_t *)(lchild->value);
      vfs_entry_t *ent = (vfs_entry_t *)(child->value);
      if (u_strcmp(ent->name, mpath + path_idx) == 0) {
        found = 1;
        node = child;
        break;
      }
    }

    if (found == 0) {
      vfs_entry_t *ent = kmalloc(sizeof(vfs_entry_t));
      u_memset(ent, 0, sizeof(vfs_entry_t));
      u_memcpy(ent->name, mpath + path_idx, u_strlen(mpath + path_idx) + 1);
      tree_node_t *child = tree_init(ent);
      tree_insert(node, child);
      node = child;
    }
  }

  vfs_entry_t *ent = (vfs_entry_t *)(node->value);
  ent->file = local_root;
  kfree(mpath);
}

// Get the mount point within a path.
static fs_node_t *get_mount_point(
  const char *path, size_t path_len, size_t *idx_out
  )
{
  size_t path_idx = 1;
  tree_node_t *node = fs_tree;
  uint8_t found = 1;
  while (found && path_idx < path_len) {
    found = 0;
    list_foreach(lchild, node->children) {
      tree_node_t *child = (tree_node_t *)(lchild->value);
      vfs_entry_t *ent = (vfs_entry_t *)(child->value);
      if (!ent) continue;

      if (u_strcmp(ent->name, path + path_idx) == 0) {
        node = child;
        path_idx += u_strlen(path + path_idx) + 1;
        found = 1;
        break;
      }
    }
  }

  vfs_entry_t *ent = (vfs_entry_t *)(node->value);
  if (!ent) return NULL;
  *idx_out = path_idx;
  return ent->file;
}

// Open the filesystem node at a path.
fs_node_t *fs_open_node(const char *path, uint32_t flags)
{
  if (fs_tree == NULL) return NULL;

  size_t path_len = u_strlen(path);
  char *mpath = kmalloc(path_len + 1);
  u_memcpy(mpath, path, path_len + 1);

  for (size_t i = 0; i < path_len; ++i)
    if (mpath[i] == FS_PATH_SEP) mpath[i] = '\0';

  size_t path_idx = 0;
  fs_node_t *mount_point = get_mount_point(mpath, path_len, &path_idx);
  fs_node_t *node = mount_point;
  while (path_idx < path_len) {
    fs_node_t *new_node = fs_finddir(node, mpath + path_idx);
    if (node != mount_point) kfree(node); // Not sure about this.
    node = new_node;
    if (node == NULL) return NULL; // File not found.
    path_idx += u_strlen(mpath + path_idx) + 1;
  }

  fs_open(node, flags);
  return node;
}
