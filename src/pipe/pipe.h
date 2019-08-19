
// pipe.h
//
// Unix-style pipe for IPC.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PIPE_H_
#define _PIPE_H_

#include <stdint.h>
#include <fs/fs.h>
#include <ringbuffer/ringbuffer.h>

typedef struct pipe_s {
  fs_node_t *read_node;
  fs_node_t *write_node;
  ringbuffer_t *rb;
  uint32_t read_refcount;
  uint32_t write_refcount;
  uint8_t read_closed;
  uint8_t write_closed;
  uint8_t read_buffered;
  uint8_t write_buffered;
} pipe_t;

uint32_t pipe_create(
  fs_node_t *read_node, fs_node_t *write_node, uint8_t rb, uint8_t wb
  );

#endif /* _PIPE_H_ */
