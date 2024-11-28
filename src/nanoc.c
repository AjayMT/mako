
// nanoc.c
//
// nanoc standard library for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "common/stdint.h"
#include "common/syscall_nums.h"
#include <stddef.h>

int32_t _syscall0(const uint32_t num)
{
  int32_t ret;
  asm volatile("movl %0, %%eax" : : "r"(num));
  asm volatile("int $0x80");
  asm volatile("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall1(const uint32_t num, const uint32_t a1)
{
  int32_t ret;
  asm volatile("movl %0, %%edi" : : "r"(a1));
  asm volatile("movl %0, %%eax" : : "r"(num));
  asm volatile("int $0x80");
  asm volatile("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall2(const uint32_t num, const uint32_t a1, const uint32_t a2)
{
  int32_t ret;
  asm volatile("movl %0, %%ecx" : : "r"(a2));
  asm volatile("movl %0, %%edi" : : "r"(a1));
  asm volatile("movl %0, %%eax" : : "r"(num));
  asm volatile("int $0x80");
  asm volatile("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall3(const uint32_t num, const uint32_t a1, const uint32_t a2, const uint32_t a3)
{
  int32_t ret;
  asm volatile("movl %0, %%edx" : : "r"(a3));
  asm volatile("movl %0, %%ecx" : : "r"(a2));
  asm volatile("movl %0, %%edi" : : "r"(a1));
  asm volatile("movl %0, %%eax" : : "r"(num));
  asm volatile("int $0x80");
  asm volatile("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall4(const uint32_t num,
                  const uint32_t a1,
                  const uint32_t a2,
                  const uint32_t a3,
                  const uint32_t a4)
{
  int32_t ret;
  asm volatile("movl %0, %%esi" : : "r"(a4));
  asm volatile("movl %0, %%edx" : : "r"(a3));
  asm volatile("movl %0, %%ecx" : : "r"(a2));
  asm volatile("movl %0, %%edi" : : "r"(a1));
  asm volatile("movl %0, %%eax" : : "r"(num));
  asm volatile("int $0x80");
  asm volatile("movl %%eax, %0" : "=r"(ret));
  return ret;
}

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

#define PAGE_SIZE 0x1000

void *malloc(size_t size)
{
  if (size != (size & 0xFFFFF000))
    size = (size & 0xFFFFF000) + PAGE_SIZE;
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
