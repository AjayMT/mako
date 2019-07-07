
#include <stdint.h>

int32_t syscall0(uint32_t num)
{
  int32_t ret;
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}

int32_t syscall1(uint32_t num, uint32_t a1)
{
  int32_t ret;
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}

int main(int argc, char *argv[])
{
  int result = syscall0(1);
  if (result == 0)
    syscall1(2, (uint32_t)"/rd/test2");

  // syscall1(0, 0xc0fffeee);
  return 0;
}
