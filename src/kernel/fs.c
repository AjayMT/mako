
// fs.c
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "fs.h"
#include "../common/errno.h"
#include "../common/stdint.h"
#include "ds.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "process.h"
#include "util.h"
#include <stddef.h>

#define CHECK(err, msg, code)                                                                      \
  if ((err)) {                                                                                     \
    log_error("fs", msg "\n");                                                                     \
    return (code);                                                                                 \
  }
#define CHECK_UNLOCK(err, msg, code)                                                               \
  if ((err)) {                                                                                     \
    log_error("fs", msg "\n");                                                                     \
    kunlock(&fs_lock);                                                                             \
    return (code);                                                                                 \
  }

// Filesystem mountpoint tree.
static tree_node_t *fs_tree = NULL;
static volatile uint32_t fs_lock = 0;

void fs_open(fs_node_t *node, uint32_t flags)
{
  if (node && node->open)
    node->open(node, flags);
}
void fs_close(fs_node_t *node)
{
  if (node && node->close)
    node->close(node);
}
int32_t fs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
  if (node && node->read)
    return node->read(node, offset, size, buffer);
  return -ENODEV;
}
int32_t fs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
  if (node && node->write)
    return node->write(node, offset, size, buffer);
  return -ENODEV;
}
struct dirent *fs_readdir(fs_node_t *node, uint32_t index)
{
  if (node == NULL || (node->flags & FS_DIRECTORY) == 0)
    return NULL;

  if (node->tree_node) {
    tree_node_t *tnode = node->tree_node;
    if (tnode != fs_tree) {
      if (index < 2) {
        struct dirent *ent = kmalloc(sizeof(struct dirent));
        if (ent == NULL)
          return NULL;
        char *name = index == 0 ? "." : "..";
        u_memcpy(ent->name, name, u_strlen(name) + 1);
        ent->ino = index;
        return ent;
      }
      index -= 2;
    }

    if (index < tnode->children->size) {
      list_foreach(lchild, tnode->children)
      {
        tree_node_t *tchild = lchild->value;
        fs_node_t *fschild = tchild->value;
        if (index == 0) {
          struct dirent *ent = kmalloc(sizeof(struct dirent));
          if (ent == NULL)
            return NULL;
          u_memcpy(ent->name, fschild->name, u_strlen(fschild->name) + 1);
          ent->ino = fschild->inode;
          return ent;
        }
        --index;
      }
    } else
      index -= tnode->children->size;
  }

  if (node->readdir)
    return node->readdir(node, index);
  return NULL;
}
fs_node_t *fs_finddir(fs_node_t *node, char *name)
{
  if (node == NULL || (node->flags & FS_DIRECTORY) == 0)
    return NULL;

  if (node->tree_node) {
    tree_node_t *tnode = node->tree_node;
    list_foreach(lchild, tnode->children)
    {
      tree_node_t *tchild = lchild->value;
      fs_node_t *fschild = tchild->value;
      if (u_strcmp(fschild->name, name) == 0)
        return fschild;
    }
  }

  if (node->finddir)
    return node->finddir(node, name);
  return NULL;
}
int32_t fs_chmod(fs_node_t *node, int32_t mask)
{
  if (node && node->chmod)
    return node->chmod(node, mask);
  return -ENODEV;
}
int32_t fs_readlink(fs_node_t *node, char *buf, size_t bufsize)
{
  if (node && (node->flags & FS_SYMLINK) == 0)
    return -ENOTDIR;
  if (node && node->readlink)
    return node->readlink(node, buf, bufsize);
  return -ENODEV;
}

// Resolve a (relative) path.
uint32_t fs_resolve_path(char **outpath, const char *inpath)
{
  process_t *current = process_current();
  uint32_t path_len = u_strlen(inpath);
  char path[path_len + 1];

  u_memcpy(path, inpath, path_len + 1);
  for (; path[path_len - 1] == FS_PATH_SEP && path_len > 1; --path_len)
    ;
  path[path_len] = '\0';
  for (uint32_t i = 0; i < path_len; ++i)
    if (path[i] == FS_PATH_SEP)
      path[i] = '\0';

  list_t *stack = kmalloc(sizeof(list_t));
  CHECK(stack == NULL, "No memory.", ENOMEM);
  u_memset(stack, 0, sizeof(list_t));

  if (path[0] != '\0') {
    if (current == NULL)
      return 1;

    char wd[u_strlen(current->wd) + 1];
    u_memcpy(wd, current->wd, u_strlen(current->wd) + 1);
    uint32_t wd_len = u_strlen(wd);
    for (uint32_t i = 0; i < wd_len; ++i)
      if (wd[i] == FS_PATH_SEP)
        wd[i] = '\0';

    for (uint32_t i = 0; i < wd_len; i += u_strlen(wd + i) + 1) {
      char *segment = kmalloc(u_strlen(wd + i) + 1);
      CHECK(segment == NULL, "No memory.", ENOMEM);
      u_memcpy(segment, wd + i, u_strlen(wd + i) + 1);
      list_push_back(stack, segment);
    }
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
  list_foreach(segment, stack)
  {
    total_length += u_strlen((char *)segment->value);
    if (segment->next || stack->size == 1)
      ++total_length;
  }

  *outpath = kmalloc(total_length + 1);
  CHECK(*outpath == NULL, "No memory.", ENOMEM);
  uint32_t i = 0;
  list_foreach(segment, stack)
  {
    uint32_t len = u_strlen((char *)segment->value);
    u_memcpy((*outpath) + i, (char *)segment->value, len);
    i += len;
    if (segment->next || stack->size == 1)
      (*outpath)[i] = FS_PATH_SEP;
    ++i;
  }
  (*outpath)[total_length] = '\0';

  list_destroy(stack);
  return 0;
}

static int32_t split_basename(char **out, char *in)
{
  char *resolved;
  uint32_t res = fs_resolve_path(&resolved, in);
  CHECK(res, "Could not resolve path.", -res);

  uint32_t dirname_length = u_strlen(resolved);
  for (; resolved[dirname_length] != FS_PATH_SEP && dirname_length > 0; --dirname_length)
    ;
  if (resolved[dirname_length] == FS_PATH_SEP)
    ++dirname_length;

  *out = resolved;
  return dirname_length;
}

// Create a symlink `dst` to `src`.
int32_t fs_symlink(char *src, char *dst)
{
  char *parent_path = NULL;
  int32_t basename_idx = split_basename(&parent_path, dst);
  CHECK(basename_idx <= 0, "Failed to split basename.", basename_idx);

  char *basename = parent_path + basename_idx;
  parent_path[basename_idx - 1] = '\0';

  fs_node_t parent;
  uint32_t err = fs_open_node(&parent, parent_path, 0);
  if (err || !parent.symlink) {
    kfree(parent_path);
    return -err;
  }

  char *rsrc;
  err = fs_resolve_path(&rsrc, src);
  CHECK(err, "Failed to errolve path.", -err);
  err = parent.symlink(&parent, rsrc, basename);
  kfree(parent_path);
  kfree(rsrc);
  return err;
}

static int32_t fs_create_or_mkdir(char *path, uint16_t mask, uint8_t is_dir)
{
  char *parent_path = NULL;
  int32_t basename_idx = split_basename(&parent_path, path);
  CHECK(basename_idx <= 0, "Failed to split basename.", basename_idx);

  char *basename = parent_path + basename_idx;
  parent_path[basename_idx - 1] = '\0';

  fs_node_t parent;
  uint32_t err = fs_open_node(&parent, parent_path, 0);
  if (err || (is_dir && !parent.mkdir) || (!is_dir && !parent.create)) {
    kfree(parent_path);
    return -err;
  }

  if (is_dir)
    err = parent.mkdir(&parent, basename, mask);
  else
    err = parent.create(&parent, basename, mask);
  kfree(parent_path);
  return err;
}

int32_t fs_mkdir(char *path, uint16_t mask)
{
  return fs_create_or_mkdir(path, mask, /* is_dir */ 1);
}

int32_t fs_create(char *path, uint16_t mask)
{
  return fs_create_or_mkdir(path, mask, /* is_dir */ 0);
}

int32_t fs_unlink(char *path)
{
  char *parent_path = NULL;
  int32_t basename_idx = split_basename(&parent_path, path);
  CHECK(basename_idx <= 0, "Failed to split basename.", basename_idx);

  char *basename = parent_path + basename_idx;
  parent_path[basename_idx - 1] = '\0';

  fs_node_t parent;
  uint32_t err = fs_open_node(&parent, parent_path, 0);
  if (err || !parent.unlink) {
    kfree(parent_path);
    return -err;
  }

  err = parent.unlink(&parent, basename);
  kfree(parent_path);
  return err;
}

int32_t fs_rename(char *old, char *new)
{
  char *parent_path = NULL;
  int32_t basename_idx = split_basename(&parent_path, old);
  CHECK(basename_idx <= 0, "Failed to split basename.", basename_idx);

  char *basename = parent_path + basename_idx;
  parent_path[basename_idx - 1] = '\0';

  fs_node_t parent;
  uint32_t err = fs_open_node(&parent, parent_path, 0);
  if (err || !parent.rename) {
    kfree(parent_path);
    return -err;
  }

  char *new_parent_path = NULL;
  int32_t new_basename_idx = split_basename(&new_parent_path, new);
  CHECK(new_basename_idx <= 0, "Failed to split basename.", new_basename_idx);

  char *new_basename = new_parent_path + new_basename_idx;
  new_parent_path[new_basename_idx - 1] = '\0';

  if (u_strcmp(parent_path, new_parent_path) != 0) {
    kfree(new_parent_path);
    kfree(parent_path);
    return -EPERM;
  }

  err = parent.rename(&parent, basename, new_basename);
  kfree(new_parent_path);
  kfree(parent_path);
  return err;
}

static fs_node_t *vfs_node_create()
{
  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  CHECK(node == NULL, "No memory.", NULL);
  u_memset(node, 0, sizeof(fs_node_t));
  node->mask = 0555;
  node->flags = FS_DIRECTORY;
  return node;
}

// Initialize the filesystem interface.
uint32_t fs_init()
{
  fs_node_t *root = vfs_node_create();
  CHECK(root == NULL, "No memory.", ENOMEM);
  fs_tree = tree_init(root);
  root->tree_node = fs_tree;
  return 0;
}

// Mount a filesystem at a path.
uint32_t fs_mount(fs_node_t *local_root, const char *path)
{
  CHECK(fs_tree == NULL, "Attempted to mount before initializing filesystem.", ENXIO);

  klock(&fs_lock);

  char *mpath;
  uint32_t res = fs_resolve_path(&mpath, path);
  CHECK(res, "Failed to resolve path.", res);
  size_t path_len = u_strlen(mpath);
  for (size_t i = 0; i < path_len; ++i)
    if (mpath[i] == FS_PATH_SEP)
      mpath[i] = '\0';

  tree_node_t *node = fs_tree;
  for (size_t path_idx = 1; path_idx < path_len; path_idx += u_strlen(mpath + path_idx) + 1) {
    uint8_t found = 0;
    list_foreach(lchild, node->children)
    {
      tree_node_t *child = (tree_node_t *)(lchild->value);
      fs_node_t *ent = (fs_node_t *)(child->value);
      if (u_strcmp(ent->name, mpath + path_idx) == 0) {
        found = 1;
        node = child;
        break;
      }
    }

    if (found == 0) {
      fs_node_t *vfs_node = NULL;
      if (path_idx + u_strlen(mpath + path_idx) + 1 < path_len) {
        vfs_node = vfs_node_create();
        CHECK_UNLOCK(vfs_node == NULL, "Could not create VFS node.", ENOMEM);
      } else
        vfs_node = local_root;
      u_memcpy(vfs_node->name, mpath + path_idx, u_strlen(mpath + path_idx) + 1);
      tree_node_t *child = tree_init(vfs_node);
      vfs_node->tree_node = child;
      tree_insert(node, child);
      node = child;
    }
  }

  if (path_len <= 1) { // Mounting at the root.
    node->value = local_root;
    local_root->tree_node = node;
    u_memcpy(local_root->name, "/", 2);
  }

  kfree(mpath);
  kunlock(&fs_lock);
  return 0;
}

// Get the mount point within a path.
static fs_node_t *get_mount_point(const char *path, size_t path_len, size_t *idx_out)
{
  size_t path_idx = 1;
  tree_node_t *node = fs_tree;
  uint8_t found = 1;
  while (found && path_idx < path_len) {
    found = 0;
    list_foreach(lchild, node->children)
    {
      tree_node_t *child = (tree_node_t *)(lchild->value);
      fs_node_t *ent = (fs_node_t *)(child->value);
      if (!ent)
        continue;

      if (u_strcmp(ent->name, path + path_idx) == 0) {
        node = child;
        path_idx += u_strlen(path + path_idx) + 1;
        found = 1;
        break;
      }
    }
  }

  fs_node_t *ent = (fs_node_t *)(node->value);
  if (!ent)
    return NULL;
  *idx_out = path_idx;
  return ent;
}

// Open the filesystem node at a path.
uint32_t fs_open_node(fs_node_t *out_node, const char *path, uint32_t flags)
{
  CHECK(fs_tree == NULL, "Attempted to open before initializing filesystem.", ENXIO);

  char *path_segments;
  uint32_t err = fs_resolve_path(&path_segments, path);
  CHECK(err, "Failed to resolve path.", err);

  size_t path_len = u_strlen(path_segments);
  for (size_t i = 0; i < path_len; ++i)
    if (path_segments[i] == FS_PATH_SEP)
      path_segments[i] = '\0';

  size_t path_idx = 0;
  fs_node_t *mount_point = get_mount_point(path_segments, path_len, &path_idx);
  if (mount_point == NULL) {
    kfree(path_segments);
    return ENOENT;
  }

  fs_node_t *node = mount_point;
  while (path_idx < path_len) {
    fs_node_t *next_node = fs_finddir(node, path_segments + path_idx);
    if (node != mount_point) {
      fs_close(node);
      kfree(node);
    }
    node = next_node;
    if (node == NULL) {
      kfree(path_segments);
      return ENOENT;
    }
    path_idx += u_strlen(path_segments + path_idx) + 1;

    if ((node->flags & FS_SYMLINK) && (flags & O_NOFOLLOW) == 0) {
      kfree(path_segments);

      char link_target[128];
      int32_t err = fs_readlink(node, link_target, sizeof(link_target));
      if (err < 0)
        return -err;

      return fs_open_node(out_node, link_target, flags);
    }
  }

  kfree(path_segments);
  fs_open(node, flags & (~O_CREAT));
  u_memcpy(out_node, node, sizeof(fs_node_t));
  return 0;
}
