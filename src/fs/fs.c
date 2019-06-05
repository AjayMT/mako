
// fs.c
//
// Filesystem interface for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include "fs.h"

const uint32_t FS_FLAGS_MASK = 7;

// TODO return/set error codes.

void open_fs(fs_node_t *node, uint32_t flags)
{ if (node && node->open) node->open(node, flags); }
void close_fs(fs_node_t *node)
{ if (node && node->close) node->close(node); }

uint32_t read_fs(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (node && node->read)
    return node->read(node, offset, size, buffer);
  return 0;
}
uint32_t write_fs(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer
  )
{
  if (node && node->read)
    return node->read(node, offset, size, buffer);
  return 0;
}

struct dirent *readdir_fs(fs_node_t *node, uint32_t index)
{
  if (node && (node->flags & FS_FLAGS_MASK) == FS_DIRECTORY && node->readdir)
    return node->readdir(node, index);
  return NULL;
}
fs_node_t *finddir_fs(fs_node_t *node, char *name)
{
  if (node && (node->flags & FS_FLAGS_MASK) == FS_DIRECTORY && node->finddir)
    return node->finddir(node, name);
  return NULL;
}
