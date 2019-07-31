
// init.c
//
// `init' process.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <mako.h>

int main(int argc, char *argv[])
{
  fopen("/stdin.txt", "r");
  fopen("/stdout.txt", "w");
  fopen("/dev/debug", "w");

  setenv("APPS_PATH", "/apps", 0);
  setenv("PATH", "/bin", 0);

  if (fork() == 0)
    execve("/apps/dex", NULL, environ);

  while (1) yield();
  return 0;
}
