
// mako.c
//
// Mako specific syscalls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <_syscall.h>
#include <mako.h>

int32_t pipe(uint32_t *readfd, uint32_t *writefd)
{
  int32_t res = _syscall2(SYSCALL_PIPE, (uint32_t)readfd, (uint32_t)writefd);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t movefd(uint32_t from, uint32_t to)
{
  int32_t res = _syscall2(SYSCALL_MOVEFD, from, to);
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

pid_t thread(thread_t t)
{
  int32_t res = _syscall1(SYSCALL_THREAD, (uint32_t)t);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t msleep(uint32_t duration)
{
  int32_t res = _syscall1(SYSCALL_MSLEEP, duration);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
