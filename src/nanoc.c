
// nanoc.c
//
// nanoc standard library for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "common/stdint.h"
#include "common/syscall_nums.h"
#include "libc/_syscall.h"
#include <stddef.h>

uint32_t write(uint32_t fd, const void *buf, uint32_t count)
{
  return _syscall3(SYSCALL_WRITE, fd, (uint32_t)buf, count);
}

uint32_t read(uint32_t fd, const void *buf, uint32_t count)
{
  return _syscall3(SYSCALL_READ, fd, (uint32_t)buf, count);
}

int32_t open(char *path, int32_t flags, uint32_t mode)
{
  return _syscall3(SYSCALL_OPEN, (uint32_t)path, (uint32_t)flags, mode);
}

int32_t close(uint32_t fd)
{
  return _syscall1(SYSCALL_CLOSE, fd);
}

uint32_t fork()
{
  return _syscall0(SYSCALL_FORK);
}

int32_t execve(char *path, char **argv, char **envp)
{
  char *largv[] = { NULL };
  char *lenvp[] = { NULL };
  if (argv == NULL)
    argv = largv;
  if (envp == NULL)
    envp = largv;
  return _syscall3(SYSCALL_EXECVE, (uint32_t)path, (uint32_t)argv, (uint32_t)envp);
}

void exit(int32_t status)
{
  _syscall1(SYSCALL_EXIT, (uint32_t)status);
}

uint32_t pagealloc(uint32_t npages)
{
  return _syscall1(SYSCALL_PAGEALLOC, npages);
}

int32_t pagefree(uint32_t vaddr, uint32_t npages)
{
  return _syscall2(SYSCALL_PAGEFREE, vaddr, npages);
}

void *malloc(size_t size)
{
  size = (size + 0xfff) & 0xfffff000;
  return (void *)pagealloc(size >> 12);
}

void free(void *ptr)
{
  return;
}

#define ARGV_ADDR 0xBFFFF000

int32_t main(int32_t argc, char *argv[]);

void _start()
{
  char **argv = (char **)ARGV_ADDR;
  int32_t argc = 0;
  while (argv[argc])
    ++argc;
  exit(main(argc, argv));
}
