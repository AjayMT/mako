
// mako.c
//
// Mako specific syscalls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <_syscall.h>
#include <stdlib.h>
#include <mako.h>

int32_t pipe(uint32_t *readfd, uint32_t *writefd, uint32_t rb, uint32_t wb)
{
  int32_t res = _syscall4(
    SYSCALL_PIPE, (uint32_t)readfd, (uint32_t)writefd, rb, wb
    );
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t movefd(uint32_t from, uint32_t to)
{
  int32_t res = _syscall2(SYSCALL_MOVEFD, from, to);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t resolve(char *out, char *in, size_t l)
{
  int32_t res = _syscall3(SYSCALL_RESOLVE, (uint32_t)out, (uint32_t)in, l);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t maketty(uint32_t fd)
{
  int32_t res = _syscall1(SYSCALL_MAKETTY, fd);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

uint32_t pagealloc(uint32_t npages)
{ return _syscall1(SYSCALL_PAGEALLOC, npages); }
int32_t pagefree(uint32_t vaddr, uint32_t npages)
{
  int32_t res = _syscall2(SYSCALL_PAGEFREE, vaddr, npages);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

static void thread_start()
{
  uint32_t ebx, ecx;
  asm volatile ("movl %%ebx, %0" : "=r"(ebx));
  asm volatile ("movl %%ecx, %0" : "=r"(ecx));
  thread_t t = (thread_t)ebx;
  t((void *)ecx);
  exit(0);
}

void _init_thread()
{ _syscall1(SYSCALL_THREAD_REGISTER, (uint32_t)thread_start); }

pid_t thread(thread_t t, void *data)
{
  int32_t res = _syscall2(SYSCALL_THREAD, (uint32_t)t, (uint32_t)data);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t msleep(uint32_t duration)
{
  int32_t res = _syscall1(SYSCALL_MSLEEP, duration);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

void yield()
{ _syscall0(SYSCALL_YIELD); }

void thread_lock(thread_lock_t l)
{
  while (__sync_lock_test_and_set(l, 1)) yield();
}

void thread_unlock(thread_lock_t l)
{ __sync_lock_release(l); }

uint32_t systime()
{ return _syscall0(SYSCALL_SYSTIME); }
