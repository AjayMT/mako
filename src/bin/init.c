
// init.c
//
// `init' process.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <mako.h>

int main(int argc, char *argv[])
{
  open("/dev/null", O_RDONLY);
  open("/dev/null", O_WRONLY);
  open("/dev/debug", O_WRONLY);

  setenv("APPS_PATH", "/apps", 0);
  setenv("PATH", "/bin", 0);
  chdir("/home");

  if (fork() == 0) {
    char *args[] = { "/home/hello.txt", NULL };
    execve("/apps/xed", args, environ);
  }

  while (1) yield();
  return 0;
}
