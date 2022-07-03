
// ls.c
//
// List current directory contents.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <dirent.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
  char *dir = ".";
  if (argc > 1) dir = argv[1];

  DIR *d = opendir(dir);
  if (d == NULL) return 1;

  struct dirent *ent = readdir(d);
  for (; ent != NULL; ent = readdir(d)) {
    size_t len = strlen(ent->d_name);
    fwrite(ent->d_name, 1, len, stdout);
    putc('\n', stdout);
  }

  return 0;
}
