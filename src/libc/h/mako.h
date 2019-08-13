
// mako.h
//
// Mako specific syscalls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _MAKO_H_
#define _MAKO_H_

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef void (*thread_t)(void *);
typedef volatile uint32_t *thread_lock_t;

int32_t pipe(uint32_t *readfd, uint32_t *writefd);
int32_t movefd(uint32_t from, uint32_t to);
int32_t resolve(char *out, char *in, size_t l);
uint32_t pagealloc(uint32_t npages);
int32_t pagefree(uint32_t vaddr, uint32_t npages);
pid_t thread(thread_t t, void *data);
int32_t msleep(uint32_t duration);
void yield();
void thread_lock(thread_lock_t);
void thread_unlock(thread_lock_t);

void _init_thread();

#endif /* _MAKO_H_ */
