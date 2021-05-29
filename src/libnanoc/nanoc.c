
// nanoc.c
//
// nanoc standard library for Mako.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <syscall/syscall_nums.h>
#include <stdint.h>

int32_t _syscall0(const uint32_t num)
{
  int32_t ret;
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall1(const uint32_t num, const uint32_t a1)
{
  int32_t ret;
  asm volatile ("movl %0, %%edi" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall2(const uint32_t num, const uint32_t a1, const uint32_t a2)
{
  int32_t ret;
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%edi" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall3(
  const uint32_t num,
  const uint32_t a1,
  const uint32_t a2,
  const uint32_t a3
  )
{
  int32_t ret;
  asm volatile ("movl %0, %%edx" : : "r"(a3));
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%edi" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall4(
  const uint32_t num,
  const uint32_t a1,
  const uint32_t a2,
  const uint32_t a3,
  const uint32_t a4
  )
{
  int32_t ret;
  asm volatile ("movl %0, %%esi" : : "r"(a4));
  asm volatile ("movl %0, %%edx" : : "r"(a3));
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%edi" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}

uint32_t write(uint32_t fd, const void *buf, uint32_t count)
{ return _syscall3(SYSCALL_WRITE, fd, (uint32_t)buf, count); }

void exit(int32_t status)
{ _syscall1(SYSCALL_EXIT, (uint32_t)status); }

#define ARGV_ADDR 0xBFFFF000

int32_t main(int32_t argc, char *argv[]);

void _start()
{
  char **argv = (char **)ARGV_ADDR;
  int32_t argc = 0;
  while (argv[argc]) ++argc;
  exit(main(argc, argv));
}
