
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
  if (syscall0(1) == 0) while (1);

  unsigned int pid = syscall0(1);
  if (pid == 0) {
    char *argv[] = { 0 };
    char *envp[] = { 0 };
    syscall3(2, (uint32_t)"/rd/test2", (uint32_t)argv, (uint32_t)envp);
  }

  syscall1(3, 1000);

  syscall3(8, pid, 12, 0);

  int fd = syscall3(10, (uint32_t)"/hello.txt", 0x200, 0666);
  syscall3(13, fd, (uint32_t)"hello world!", 12);

  return 0;
}
