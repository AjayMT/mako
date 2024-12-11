
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
  if (argc <= 1) {
    printf("Usage: read <filename>\n");
    return 1;
  }

  char *path = argv[1];
  struct stat buf;
  int32_t err = stat(path, &buf);
  if (err)
    return 1;

  char *text = malloc(buf.st_size);
  if (text == NULL)
    return 1;

  FILE *f = fopen(path, "r");
  if (f == NULL)
    return 1;

  size_t nread = fread(text, 1, buf.st_size, f);
  size_t nwritten = fwrite(text, 1, nread, stdout);
  while (nwritten < nread)
    nwritten += fwrite(text + nwritten, 1, nread - nwritten, stdout);

  while (nread < (size_t)buf.st_size) {
    nread += fread(text, 1, buf.st_size - nread, f);
    nwritten = fwrite(text, 1, nread, f);
    while (nwritten < nread)
      nwritten += fwrite(text + nwritten, 1, nread - nwritten, stdout);
  }

  return 0;
}
