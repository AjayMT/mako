
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
  fopen("/rd/test.lua", "r");
  fopen("stdout.txt", "w");
  fopen("/dev/debug", "w");

  if (fork() == 0) {
    char *args[] = { "lua", "-", NULL };
    execve("/rd/lua", args, NULL);
    printf("errno: %d\n", errno);
  }

  while (1);

  return 0;
}
