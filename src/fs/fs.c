
// fs.c
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <kheap/kheap.h>
#include <klock/klock.h>
#include <ds/ds.h>
#include <common/errno.h>
#include <util/util.h>
#include <debug/log.h>
#include <process/process.h>
#include "fs.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("fs", msg "\n"); return (code);   \
  }
#define CHECK_UNLOCK(err, msg, code) if ((err)) {   \
    log_error("fs", msg "\n"); kunlock(&fs_lock);   \
    return (code);                                  \
  }

const uint32_t FS_FLAGS_MASK = 7;

// TODO return/set error codes.

// Filesystem mountpoint tree.
static tree_node_t *fs_tree = NULL;
static volatile uint32_t fs_lock = 0;

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
  return -ENODEV;
}
uint32_t fs_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (node && node->write)
    return node->write(node, offset, size, buffer);
  return -ENODEV;
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
int32_t fs_chmod(fs_node_t *node, int32_t mask)
{
  if (node && node->chmod) return node->chmod(node, mask);
  return -ENODEV;
}
int32_t fs_readlink(fs_node_t *node, char *buf, size_t bufsize)
{
  if (node && node->readlink) return node->readlink(node, buf, bufsize);
  return -ENODEV;
}

// Resolve a (relative) path.
static uint32_t resolve_path(char **outpath, const char *inpath)
{
  uint32_t path_len = u_strlen(inpath);
  char *path = kmalloc(path_len + 1);
  CHECK(path == NULL, "No memory.", ENOMEM);
  u_memcpy(path, inpath, path_len + 1);
  for (uint32_t i = 0; i < path_len; ++i)
    if (path[i] == FS_PATH_SEP) path[i] = '\0';

  list_t *stack = kmalloc(sizeof(list_t));
  CHECK(stack == NULL, "No memory.", ENOMEM);
  u_memset(stack, 0, sizeof(list_t));

  if (path[0] != '\0') {
    if (process_current() == NULL) { kfree(path); return 1; }
    char *wd = kmalloc(u_strlen(process_current()->wd) + 1);
    CHECK(wd == NULL, "No memory.", ENOMEM);
    u_memcpy(wd, process_current()->wd, u_strlen(process_current()->wd) + 1);
    uint32_t wd_len = u_strlen(wd);
    for (uint32_t i = 0; i < wd_len; ++i)
      if (wd[i] == FS_PATH_SEP) wd[i] = '\0';
    for (uint32_t i = 0; i < wd_len; i += u_strlen(wd + i) + 1) {
      char *segment = kmalloc(u_strlen(wd + i) + 1);
      CHECK(segment == NULL, "No memory.", ENOMEM);
      u_memcpy(segment, wd + i, u_strlen(wd + i) + 1);
      list_push_back(stack, segment);
    }
    kfree(wd);
  }

  for (uint32_t i = 0; i < path_len; i += u_strlen(path + i) + 1) {
    char *segment = kmalloc(u_strlen(path + i) + 1);
    CHECK(segment == NULL, "No memory.", ENOMEM);
    u_memcpy(segment, path + i, u_strlen(path + i) + 1);
    list_push_back(stack, segment);
    if (u_strcmp(segment, FS_DIR_SELF) == 0)
      list_pop_back(stack);
    else if (u_strcmp(segment, FS_DIR_UP) == 0) {
      list_pop_back(stack);
      CHECK(stack->size == 0, "Cannot go up from root directory.", 1);
      list_pop_back(stack);
    }
  }

  uint32_t total_length = 0;
  list_foreach(segment, stack) {
    total_length += u_strlen((char *)segment->value);
    if (segment->next || stack->size == 1) ++total_length;
  }

  *outpath = kmalloc(total_length + 1);
  CHECK(*outpath == NULL, "No memory.", ENOMEM);
  uint32_t i = 0;
  list_foreach(segment, stack) {
    uint32_t len = u_strlen((char *)segment->value);
    u_memcpy((*outpath) + i, (char *)segment->value, len);
    i += len;
    if (segment->next || stack->size == 1)
      (*outpath)[i] = FS_PATH_SEP;
    ++i;
  }
  (*outpath)[total_length] = '\0';

  list_destroy(stack);
  kfree(path);
  return 0;
}

// Create a symlink `dst` to `src`.
int32_t fs_symlink(char *src, char *dst)
{
  char *dstpath;
  uint32_t res = resolve_path(&dstpath, dst);
  CHECK(res, "Could not resolve path.", -res);

  char *basename = dstpath + u_strlen(dstpath) - 1;
  for (; *basename != FS_PATH_SEP && basename >= dstpath; --basename);
  if (*basename == FS_PATH_SEP) ++basename;

  char *parent_path = kmalloc(basename - dstpath);
  CHECK(parent_path == NULL, "No memory.", -ENOMEM);
  u_memcpy(parent_path, dstpath, basename - dstpath);
  parent_path[basename - dstpath - 1] = '\0';

  fs_node_t *parent = kmalloc(sizeof(fs_node_t));
  CHECK(parent == NULL, "No memory.", -ENOMEM);
  res = fs_open_node(parent, parent_path, 0);
  if (res) {
    kfree(parent);
    kfree(parent_path);
    kfree(dstpath);
    return -res;
  }

  if (parent->symlink) {
    char *rsrc;
    res = resolve_path(&rsrc, src);
    CHECK(res, "Failed to resolve path.", -res);
    res = parent->symlink(parent, rsrc, basename);
    kfree(parent);
    kfree(parent_path);
    kfree(dstpath);
    kfree(rsrc);
    return res;
  }

  kfree(parent);
  kfree(parent_path);
  kfree(dstpath);
  return 0;
}

int32_t fs_mkdir(char *path, uint16_t mask)
{
  char *rpath;
  uint32_t res = resolve_path(&rpath, path);
  CHECK(res, "Could not resolve path.", -res);

  char *basename = rpath + u_strlen(rpath) - 1;
  for (; *basename != FS_PATH_SEP && basename >= rpath; --basename);
  if (*basename == FS_PATH_SEP) ++basename;

  char *parent_path = kmalloc(basename - rpath);
  CHECK(parent_path == NULL, "No memory.", -ENOMEM);
  u_memcpy(parent_path, rpath, basename - rpath);
  parent_path[basename - rpath - 1] = '\0';

  fs_node_t *parent = kmalloc(sizeof(fs_node_t));
  CHECK(parent == NULL, "No memory.", -ENOMEM);
  res = fs_open_node(parent, parent_path, 0);
  if (res) {
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return -res;
  }

  if (parent->mkdir) {
    res = parent->mkdir(parent, basename, mask);
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return res;
  }

  kfree(parent_path);
  kfree(parent);
  kfree(rpath);
  return 0;
}

// TODO Make create and mkdir one function.
int32_t fs_create(char *path, uint16_t mask)
{
  char *rpath;
  uint32_t res = resolve_path(&rpath, path);
  CHECK(res, "Could not resolve path.", -res);

  char *basename = rpath + u_strlen(rpath) - 1;
  for (; *basename != FS_PATH_SEP && basename >= rpath; --basename);
  if (*basename == FS_PATH_SEP) ++basename;

  char *parent_path = kmalloc(basename - rpath);
  CHECK(parent_path == NULL, "No memory.", -ENOMEM);
  u_memcpy(parent_path, rpath, basename - rpath);
  parent_path[basename - rpath - 1] = '\0';

  fs_node_t *parent = kmalloc(sizeof(fs_node_t));
  CHECK(parent == NULL, "No memory.", -ENOMEM);
  res = fs_open_node(parent, parent_path, 0);
  if (res) {
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return -res;
  }

  if (parent->create) {
    res = parent->create(parent, basename, mask);
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return res;
  }

  kfree(parent_path);
  kfree(parent);
  kfree(rpath);
  return 0;
}

int32_t fs_unlink(char *path)
{
  char *rpath;
  uint32_t res = resolve_path(&rpath, path);
  CHECK(res, "Could not resolve path.", -res);

  char *basename = rpath + u_strlen(rpath) - 1;
  for (; *basename != FS_PATH_SEP && basename >= rpath; --basename);
  if (*basename == FS_PATH_SEP) ++basename;

  char *parent_path = kmalloc(basename - rpath);
  CHECK(parent_path == NULL, "No memory.", -ENOMEM);
  u_memcpy(parent_path, rpath, basename - rpath);
  parent_path[basename - rpath - 1] = '\0';

  fs_node_t *parent = kmalloc(sizeof(fs_node_t));
  CHECK(parent == NULL, "No memory.", -ENOMEM);
  res = fs_open_node(parent, parent_path, 0);
  if (res) {
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return -res;
  }

  if (parent->unlink) {
    res = parent->unlink(parent, basename);
    kfree(parent_path);
    kfree(parent);
    kfree(rpath);
    return res;
  }

  kfree(parent_path);
  kfree(parent);
  kfree(rpath);
  return 0;
}

// readdir for mountpoints.
static struct dirent *vfs_readdir(fs_node_t *node, uint32_t idx)
{
  struct dirent *d = kmalloc(sizeof(struct dirent));
  CHECK(d == NULL, "No memory.", NULL);

  if (idx == 0) {
    u_memcpy(d->name, ".", 2);
    d->ino = 0;
    return d;
  } else if (idx == 1) {
    u_memcpy(d->name, "..", 3);
    d->ino = 1;
    return d;
  }

  tree_node_t *tnode = node->device;
  CHECK(tnode == NULL, "FS node without associated tree node.", NULL);
  idx -= 2;
  uint32_t child_idx = 0;
  list_foreach(lchild, tnode->children) {
    if (child_idx != idx) { ++child_idx; continue; }

    tree_node_t *tchild = (tree_node_t *)lchild->value;
    fs_node_t *cnode = tchild->value;
    CHECK(cnode == NULL, "Tree node without associated FS node.", NULL);

    u_memcpy(d->name, cnode->name, u_strlen(cnode->name) + 1);
    d->ino = child_idx + 2;
    return d;
  }

  return NULL;
}

static fs_node_t *vfs_node_create()
{
  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  CHECK(node == NULL, "No memory.", NULL);
  u_memset(node, 0, sizeof(fs_node_t));
  node->mask = 0555;
  node->flags = FS_DIRECTORY;
  node->readdir = vfs_readdir;
  return node;
}

// Initialize the filesystem interface.
uint32_t fs_init()
{
  vfs_entry_t *vfs_root = kmalloc(sizeof(vfs_entry_t));
  CHECK(vfs_root == NULL, "No memory.", ENOMEM);
  u_memset(vfs_root, 0, sizeof(vfs_entry_t));
  fs_tree = tree_init(vfs_root);
  return 0;
}

// Mount a filesystem at a path.
uint32_t fs_mount(fs_node_t *local_root, const char *path)
{
  CHECK(
    fs_tree == NULL,
    "Attempted to mount before initializing filesystem.",
    ENXIO
    );

  klock(&fs_lock);

  char *mpath;
  uint32_t res = resolve_path(&mpath, path);
  CHECK(res, "Failed to resolve path.", res);
  size_t path_len = u_strlen(mpath);
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
      CHECK_UNLOCK(ent == NULL, "No memory.", ENOMEM);
      u_memset(ent, 0, sizeof(vfs_entry_t));
      u_memcpy(ent->name, mpath + path_idx, u_strlen(mpath + path_idx) + 1);
      fs_node_t *vfs_node = vfs_node_create();
      CHECK_UNLOCK(vfs_node == NULL, "Could not create VFS node.", ENOMEM);
      u_memcpy(vfs_node->name, ent->name, u_strlen(ent->name) + 1);
      tree_node_t *child = tree_init(ent);
      vfs_node->device = child;
      ent->file = vfs_node;
      tree_insert(node, child);
      node = child;
    }
  }

  vfs_entry_t *ent = (vfs_entry_t *)(node->value);
  ent->file = local_root;
  kfree(mpath);
  kunlock(&fs_lock);
  return 0;
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
uint32_t fs_open_node(fs_node_t *out_node, const char *path, uint32_t flags)
{
  CHECK(
    fs_tree == NULL,
    "Attempted to open before initializing filesystem.",
    ENXIO
    );

  char *mpath;
  uint32_t res = resolve_path(&mpath, path);
  CHECK(res, "Failed to resolve path.", res);
  size_t path_len = u_strlen(mpath);
  for (size_t i = 0; i < path_len; ++i)
    if (mpath[i] == FS_PATH_SEP) mpath[i] = '\0';

  size_t path_idx = 0;
  fs_node_t *mount_point = get_mount_point(mpath, path_len, &path_idx);
  if (mount_point == NULL) { kfree(mpath); return ENOENT; }

  fs_node_t *node = mount_point;
  while (path_idx < path_len) {
    // TODO symlinks.

    fs_node_t *new_node = fs_finddir(node, mpath + path_idx);
    if (node != mount_point) kfree(node); // Not sure about this.
    node = new_node;
    if (node == NULL) { kfree(mpath); return ENOENT; }
    path_idx += u_strlen(mpath + path_idx) + 1;
  }

  fs_open(node, flags);
  u_memcpy(out_node, node, sizeof(fs_node_t));
  kfree(mpath);
  return 0;
}
