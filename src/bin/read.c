
// read.c
//
// Read file.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
  if (argc <= 1)
    return 1;

  char *path = argv[1];
  struct stat buf;
  int32_t res = stat(path, &buf);
  if (res)
    return 1;

  char *text = malloc(buf.st_size + 1);
  FILE *f = fopen(path, "r");
  fread(text, buf.st_size, 1, f);
  text[buf.st_size] = 0;

  fwrite(text, buf.st_size, 1, stdout);

  return 0;
}
