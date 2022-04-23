
// ls.c
//
// List current directory contents.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <dirent.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
  char *dir = ".";
  if (argc > 1) dir = argv[1];

  DIR *d = opendir(dir);
  if (d == NULL) return 1;

  struct dirent *ent = readdir(d);
  for (; ent != NULL; ent = readdir(d))
    printf("%s\n", ent->d_name);

  return 0;
}
