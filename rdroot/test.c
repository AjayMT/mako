
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char *argv[])
{
  fopen("/rd/test.lua", "r");
  fopen("stdout.txt", "w");
  fopen("/dev/debug", "w");

  if (fork() == 0)
    execve("/rd/test2", NULL, NULL);

  while (1);

  return 0;
}
