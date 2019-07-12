
// ringbuffer.c
//
// Ring buffer for device I/O, pipes, etc.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <kheap/kheap.h>
#include <ds/ds.h>
#include <process/process.h>
#include <interrupt/interrupt.h>
#include <util/util.h>
#include "ringbuffer.h"

ringbuffer_t *ringbuffer_create(uint32_t size)
{
  ringbuffer_t *rb = kmalloc(sizeof(ringbuffer_t));
  if (rb == NULL) return NULL;
  u_memset(rb, 0, sizeof(ringbuffer_t));
  uint8_t *buffer = kmalloc(size);
  if (buffer == NULL) { kfree(rb); return NULL; }
  rb->buffer = buffer;
  rb->size = size;
  rb->readers = kmalloc(sizeof(list_t));
  if (rb->readers == NULL) { kfree(buffer); kfree(rb); return NULL; }
  u_memset(rb->readers, 0, sizeof(list_t));
  rb->writers = kmalloc(sizeof(list_t));
  if (rb->readers == NULL) {
    kfree(buffer); kfree(rb); kfree(rb->readers);
    return NULL;
  }
  u_memset(rb->writers, 0, sizeof(list_t));

  return rb;
}

void ringbuffer_close_write(ringbuffer_t *rb)
{
  uint32_t eflags = interrupt_save_disable();
  rb->write_closed = 1;
  while (rb->readers->size) {
    list_node_t *head = rb->readers->head;
    process_t *p = head->value;
    p->is_running = 1;
    // process_schedule(p);
    list_remove(rb->readers, head, 0);
    kfree(head);
  }
  interrupt_restore(eflags);
}

uint32_t ringbuffer_check_read(ringbuffer_t *rb)
{
  if (rb->read_idx > rb->write_idx)
    return rb->write_idx + (rb->size - rb->read_idx);
  return rb->write_idx - rb->read_idx;
}

uint32_t ringbuffer_check_write(ringbuffer_t *rb)
{
  if (rb->read_idx > rb->write_idx)
    return rb->read_idx - rb->write_idx - 1;
  return rb->size - rb->write_idx + rb->read_idx - 1;
}

void ringbuffer_wait_read(ringbuffer_t *rb, process_registers_t regs)
{
  process_t *current = process_current();
  if (!current) return;
  regs.esp += 16;
  u_memcpy(&(current->kregs), &regs, sizeof(process_registers_t));
  current->is_running = 0;
  list_push_back(rb->readers, current);
  process_switch_next();
}

uint32_t ringbuffer_finish_read(ringbuffer_t *rb, uint32_t size, uint8_t *buf)
{
  if (ringbuffer_check_read(rb) < size)
    size = ringbuffer_check_read(rb);

  for (uint32_t i = 0; i < size; ++i) {
    rb->read_idx = (rb->read_idx + i) % rb->size;
    buf[i] = rb->buffer[rb->read_idx];
  }

  while (rb->writers->size) {
    list_node_t *head = rb->writers->head;
    process_t *p = head->value;
    p->is_running = 1;
    // process_schedule(p);
    list_remove(rb->writers, head, 0);
    kfree(head);
  }

  return size;
}

void ringbuffer_wait_write(ringbuffer_t *rb, process_registers_t regs)
{
  process_t *current = process_current();
  if (!current) return;
  regs.esp += 16;
  u_memcpy(&(current->kregs), &regs, sizeof(process_registers_t));
  current->is_running = 0;
  list_push_back(rb->writers, current);
  process_switch_next();
}

uint32_t ringbuffer_finish_write(
  ringbuffer_t *rb, uint32_t size, uint8_t *buf
  )
{
  if (ringbuffer_check_write(rb) < size)
    size = ringbuffer_check_write(rb);

  for (uint32_t i = 0; i < size; ++i) {
    rb->write_idx = (rb->write_idx + i) % rb->size;
    rb->buffer[rb->write_idx] = buf[i];
  }

  while (rb->readers->size) {
    list_node_t *head = rb->readers->head;
    process_t *p = head->value;
    p->is_running = 1;
    // process_schedule(p);
    list_remove(rb->readers, head, 0);
    kfree(head);
  }

  return size;
}

void ringbuffer_destroy(ringbuffer_t *rb)
{
  while (rb->readers->size)
    list_remove(rb->readers, rb->readers->head, 0);
  while (rb->writers->size)
    list_remove(rb->writers, rb->writers->head, 0);
  list_destroy(rb->readers);
  list_destroy(rb->writers);
  kfree(rb->buffer);
  kfree(rb);
}
