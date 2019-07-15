
#include <stdint.h>

int32_t syscall1(uint32_t num, uint32_t a1)
{
  int32_t ret;
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}

void sighandler()
{
  uint32_t signum;
  asm volatile ("movl %%ebx, %0" : "=r"(signum));
  syscall1(4, signum);
  syscall1(7, 0);
}

int main (int argc, char const *argv[])
{
  syscall1(6, (uint32_t)sighandler);
  while (1);
  return 0;
}
