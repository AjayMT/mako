
// pipe.c
//
// Pipe for IPC.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "pipe.h"
#include "kheap.h"
#include "util.h"
#include "process.h"
#include "../common/signal.h"
#include "../common/errno.h"
#include "log.h"
#include "klock.h"
#include "interrupt.h"

#define CHECK(err, msg, code) if ((err)) {      \
    log_error("pipe", msg "\n"); return (code); \
  }

#define MAX_READERS 4
static const uint32_t DEFAULT_SIZE = 1024;

typedef struct {
  process_t *process;
  uint32_t size;
} pipe_reader_t;

typedef struct {
  fs_node_t *read_node;
  fs_node_t *write_node;
  uint8_t *buf;
  uint32_t head;
  uint32_t count;
  uint32_t size;
  uint8_t write_closed;
  volatile uint32_t lock;
  pipe_reader_t readers[MAX_READERS];
  volatile uint32_t readers_lock;
} pipe_t;

uint32_t pipe_suspend(process_registers_t *regs, uint32_t updated);

static void pipe_wait(pipe_t *self, uint32_t size)
{
  process_t *current_process = process_current();

  klock(&(self->readers_lock));
  uint8_t updated = 0;
  for (uint32_t i = 0; i < MAX_READERS; ++i) {
    if (self->readers[i].process == NULL) {
      self->readers[i].process = current_process;
      self->readers[i].size = size;
      updated = 1;
      break;
    }
  }

  disable_interrupts();
  kunlock(&(self->readers_lock));

  if (updated) process_unschedule(current_process);
  pipe_suspend(&(current_process->kregs), updated);
}

static uint32_t pipe_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf)
{
  (void)offset;
  pipe_t *self = node->device;

  klock(&(self->lock));

  while (self->count == 0 && !self->write_closed) {
    kunlock(&(self->lock));
    pipe_wait(self, size);
    klock(&(self->lock));
  }

  if (size > self->count) size = self->count;
  for (uint32_t i = 0; i < size; ++i) {
    buf[i] = self->buf[self->head];
    self->head = (self->head + 1) % self->size;
  }
  self->count -= size;
  self->read_node->length = self->count;
  self->write_node->length = self->count;
  kunlock(&(self->lock));

  return size;
}

static uint32_t pipe_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf)
{
  (void)offset;
  pipe_t *self = node->device;

  if (self->buf == NULL) {
    process_signal_pid(process_current()->pid, SIGPIPE);
    return 0;
  }

  klock(&(self->lock));
  uint32_t space = self->size - self->count;
  if (size > space) size = space;
  for (uint32_t i = 0; i < size; ++i) {
    uint32_t buf_idx = (self->head + self->count + i) % self->size;
    self->buf[buf_idx] = buf[i];
  }
  self->count += size;
  self->read_node->length = self->count;
  self->write_node->length = self->count;
  kunlock(&(self->lock));

  klock(&(self->readers_lock));
  uint32_t remaining_size = size;
  for (uint32_t i = 0; i < MAX_READERS; ++i) {
    if (self->readers[i].process == NULL) continue;

    process_schedule(self->readers[i].process);
    self->readers[i].process = NULL;
    if (self->readers[i].size >= remaining_size) break;
    else remaining_size -= self->readers[i].size;
  }
  kunlock(&(self->readers_lock));

  return size;
}

static void pipe_close_read(fs_node_t *node)
{
  pipe_t *self = node->device;

  klock(&(self->lock));
  kfree(self->buf);
  self->buf = NULL;
  self->size = 0;
  self->count = 0;
  self->head = 0;
  uint32_t wc = self->write_closed;
  kunlock(&self->lock);

  if (wc) {
    kfree(self);
    node->device = NULL;
    node->length = 0;
  }
}

static void pipe_close_write(fs_node_t *node)
{
  pipe_t *self = node->device;

  klock(&self->lock);
  if (self->buf == NULL) {
    kunlock(&self->lock);
    kfree(self);
    node->device = NULL;
    node->length = 0;
    return;
  }
  self->write_closed = 1;
  kunlock(&self->lock);

  klock(&(self->readers_lock));
  for (uint32_t i = 0; i < MAX_READERS; ++i) {
    if (self->readers[i].process == NULL) continue;

    process_schedule(self->readers[i].process);
    self->readers[i].process = NULL;
  }
  kunlock(&(self->readers_lock));
}

uint32_t pipe_create(fs_node_t *read_node, fs_node_t *write_node)
{
  pipe_t *pipe = kmalloc(sizeof(pipe_t));
  CHECK(pipe == NULL, "No memory.", ENOMEM);
  u_memset(pipe, 0, sizeof(pipe_t));
  pipe->buf = kmalloc(DEFAULT_SIZE);
  CHECK(pipe->buf == NULL, "No memory.", ENOMEM);
  u_memset(pipe->buf, 0, DEFAULT_SIZE);
  pipe->size = DEFAULT_SIZE;
  pipe->read_node = read_node;
  pipe->write_node = write_node;

  read_node->device = pipe;
  read_node->read = pipe_read;
  read_node->close = pipe_close_read;

  write_node->device = pipe;
  write_node->write = pipe_write;
  write_node->close = pipe_close_write;
  return 0;
}
