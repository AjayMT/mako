
// init.c
//
// `init' process.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <fcntl.h>
#include <mako.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

void launcher_thread()
{
  int32_t unused = 0;
  char *args[] = { NULL };
  pid_t child = fork();
  if (child == 0)
    execve("/apps/launcher", args, environ);

  while (1) {
    int32_t err = waitpid(child, &unused, 0);
    if (err < 0)
      continue;
    child = fork();
    if (child == 0)
      execve("/apps/launcher", args, environ);
  }
}

int main(int argc, char *argv[])
{
  open("/dev/null", O_RDONLY);
  open("/dev/null", O_WRONLY);
  open("/dev/debug", O_WRONLY);

  setenv("APPS_PATH", "/apps", 0);
  setenv("PATH", "/bin", 0);
  chdir("/home");

  thread(launcher_thread, NULL);

  while (1)
    ;

  return 0;
}
