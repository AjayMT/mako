
// _syscall.c
//
// Syscall interrupt interface.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <_syscall.h>
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
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t _syscall2(const uint32_t num, const uint32_t a1, const uint32_t a2)
{
  int32_t ret;
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
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
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
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
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
