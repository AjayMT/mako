
#include <stdint.h>
#include <_syscall.h>

int main(int argc, char *unused[])
{
  if (_syscall0(SYSCALL_FORK) == 0) while (1);

  char k[4000];

  int readfd, writefd;
  _syscall2(SYSCALL_PIPE, (uint32_t)&readfd, (uint32_t)&writefd);
  unsigned int pid = _syscall0(SYSCALL_FORK);
  if (pid == 0) {
    char buf[128];
    _syscall3(SYSCALL_READ, readfd, (uint32_t)buf, 6);
    _syscall3(SYSCALL_OPEN, (uint32_t)buf, 0x200, 0666);

    /* char *argv[] = { 0 }; */
    /* char *envp[] = { 0 }; */
    /* _syscall3(2, (uint32_t)"/rd/test2", (uint32_t)argv, (uint32_t)envp); */
  }

  _syscall1(SYSCALL_MSLEEP, 100);
  _syscall3(SYSCALL_WRITE, writefd, (uint32_t)"asdfg", 6);

  return 0;
}
