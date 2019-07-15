
#include <stdint.h>
#include <_syscall.h>

int main(int argc, char *unused[])
{
  if (_syscall0(1) == 0) while (1);

  unsigned int pid = _syscall0(1);
  if (pid == 0) {
    char *argv[] = { 0 };
    char *envp[] = { 0 };
    _syscall3(2, (uint32_t)"/rd/test2", (uint32_t)argv, (uint32_t)envp);
  }

  _syscall1(3, 1000);

  _syscall3(8, pid, 12, 0);

  int fd = _syscall3(10, (uint32_t)"/hello.txt", 0x200, 0666);
  _syscall3(13, fd, (uint32_t)"hello world!", 12);

  return 0;
}
