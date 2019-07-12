
// ringbuffer.h
//
// Ring buffer for device I/O, pipes, etc.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <stdint.h>
#include <ds/ds.h>

struct ringbuffer_s {
  uint8_t *buffer;
  uint32_t size;
  uint32_t read_idx;
  uint32_t write_idx;
  uint32_t write_closed;
  list_t *readers;
  list_t *writers;
} __attribute__((packed));
typedef struct ringbuffer_s ringbuffer_t;

ringbuffer_t *ringbuffer_create(uint32_t);
void ringbuffer_close_write(ringbuffer_t *rb);
void ringbuffer_destroy(ringbuffer_t *);

// Implemented in ringbuffer.s.
uint32_t ringbuffer_read(ringbuffer_t *rb, uint32_t size, uint8_t *buf);
uint32_t ringbuffer_write(ringbuffer_t *rb, uint32_t size, uint8_t *buf);

#endif /* _RINGBUFFER_H_ */
