
// init.c
//
// `init' process.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <fcntl.h>
#include <mako.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  open("/dev/null", O_RDONLY);
  open("/dev/null", O_WRONLY);
  open("/dev/debug", O_WRONLY);

  setenv("APPS_PATH", "/apps", 0);
  setenv("PATH", "/bin", 0);
  chdir("/home");

  if (fork() == 0) {
    char *args[] = { NULL };
    execve("/apps/pie", args, environ);
  }

  while (1)
    ;
  return 0;
}
