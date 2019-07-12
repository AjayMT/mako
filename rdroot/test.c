
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

int32_t syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3)
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

int main(int argc, char *unused[])
{
  int result = syscall0(1);
  char *argv[] = { "hello", 0 };
  char *envp[] = { "world", "PATH=/bin", 0 };
  if (result == 0)
    syscall3(2, (uint32_t)"/rd/test2", (uint32_t)argv, (uint32_t)envp);

  // syscall1(0, 0xc0fffeee);
  return 0;
}
