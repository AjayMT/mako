
// pwd.c
//
// Print current directory.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
  char *wd = getcwd(NULL, 0);
  if (wd == NULL)
    return 1;
  printf("%s\n", wd);
  return 0;
}
