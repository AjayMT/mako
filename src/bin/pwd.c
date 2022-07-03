
// pwd.c
//
// Print current directory.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
  char *wd = getcwd(NULL, 0);
  if (wd == NULL) return 1;
  size_t len = strlen(wd);
  fwrite(wd, 1, len, stdout);
  return 0;
}
