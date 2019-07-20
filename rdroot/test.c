
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  fopen("stdin.txt", "r");
  fopen("stdout.txt", "w");
  fopen("stderr.txt", "w");

  if (fork() == 0) execve("/rd/test2", NULL, NULL);

  while (1);

  return 0;
}
