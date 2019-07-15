
#include <stdint.h>
#include <_syscall.h>

void sighandler()
{
  uint32_t signum;
  asm volatile ("movl %%ebx, %0" : "=r"(signum));
  _syscall1(4, signum);
  _syscall1(7, 0);
}

int main (int argc, char const *argv[])
{
  _syscall1(6, (uint32_t)sighandler);
  while (1);
  return 0;
}
