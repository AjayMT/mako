
// pwd.c
//
// Print current directory.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
  char *wd = getcwd(NULL, 0);
  if (wd == NULL) return 1;
  printf("%s\n", wd);
  return 0;
}
