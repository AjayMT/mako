
// pipe.c
//
// Unix-style pipe for IPC.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "../common/stdint.h"
#include "kheap.h"
#include "process.h"
#include "../common/errno.h"
#include "../common/signal.h"
#include "util.h"
#include "log.h"
#include "fs.h"
#include "ringbuffer.h"
#include "pipe.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("pipe", msg "\n"); return (code); \
  }

static const uint32_t PIPE_SIZE = 512;

static void pipe_destroy(pipe_t *pipe)
{
  ringbuffer_destroy(pipe->rb);
  pipe->rb = NULL;
}

static uint32_t pipe_read(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  pipe_t *self = node->device;
  if (self == NULL || self->rb == NULL) return 0;
  if (self->read_buffered == 0) {
    uint32_t s;
    while ((s = ringbuffer_check_read(self->rb)) == 0)
      if (self->write_closed) return 0;
    if (s > size) s = size;
    uint32_t r = ringbuffer_read(self->rb, s, buf);
    return r;
  }

  uint32_t read_size = 0;
  while (read_size < size) {
    if (self->write_closed) return read_size;
    uint32_t r = ringbuffer_read(self->rb, 1, buf + read_size);
    if (r && ((char *)buf)[read_size] == '\n')
      return read_size + r;
    read_size += r;
  }

  return read_size;
}

static uint32_t pipe_write(
  fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf
  )
{
  pipe_t *self = node->device;
  if (self == NULL || self->rb == NULL) return 0;
  if (self->write_buffered == 0) {
    uint32_t s;
    while ((s = ringbuffer_check_write(self->rb)) == 0)
      if (self->read_closed) {
        process_signal(process_current(), SIGPIPE);
        return 0;
      }
    if (s > size) s = size;
    uint32_t r = ringbuffer_write(self->rb, s, buf);
    return r;
  }

  uint32_t written_size = 0;
  while (written_size < size) {
    if (self->read_closed) {
      process_signal(process_current(), SIGPIPE);
      return written_size;
    }
    uint32_t s = ringbuffer_check_write(self->rb);
    if (s > size - written_size) s = size - written_size;
    uint32_t w = ringbuffer_write(self->rb, s, buf + written_size);
    written_size += w;
  }

  return written_size;
}

static void pipe_close_read(fs_node_t *node)
{
  pipe_t *self = node->device;
  if (self == NULL) return;
  --(self->read_refcount);
  if (self->read_refcount) return;
  self->read_closed = 1;
  if (self->write_closed) pipe_destroy(self);
}

static void pipe_close_write(fs_node_t *node)
{
  pipe_t *self = node->device;
  if (self == NULL) return;
  --(self->write_refcount);
  if (self->write_refcount) return;
  self->write_closed = 1;
  if (self->read_closed) pipe_destroy(self);
  else ringbuffer_close_write(self->rb);
}

uint32_t pipe_create(
  fs_node_t *read_node, fs_node_t *write_node, uint8_t rb, uint8_t wb
  )
{
  pipe_t *pipe = kmalloc(sizeof(pipe_t));
  CHECK(pipe == NULL, "No memory.", ENOMEM);
  u_memset(pipe, 0, sizeof(pipe_t));
  pipe->rb = ringbuffer_create(PIPE_SIZE);
  CHECK(pipe->rb == NULL, "No memory.", ENOMEM);
  pipe->read_node = read_node;
  pipe->write_node = write_node;
  pipe->read_refcount = 1;
  pipe->write_refcount = 1;
  pipe->read_buffered = rb;
  pipe->write_buffered = wb;

  u_memcpy(read_node->name, "pipe", 5);
  u_memcpy(write_node->name, "pipe", 5);
  read_node->device = pipe;
  write_node->device = pipe;
  read_node->read = pipe_read;
  write_node->write = pipe_write;
  read_node->close = pipe_close_read;
  write_node->close = pipe_close_write;
  read_node->mask = 0666;
  write_node->mask = 0666;
  read_node->flags |= FS_PIPE;
  write_node->flags |= FS_PIPE;

  return 0;
}
