
// wr.c
//
// Read file and write to stdout or another file.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
  if (argc <= 1) {
    printf("Usage: wr <in_file> [<out_file>]\n");
    return 1;
  }

  char *path = argv[1];
  struct stat buf;
  int32_t err = stat(path, &buf);
  if (err)
    return 1;

  FILE *f = fopen(path, "r");
  if (f == NULL)
    return 1;

  FILE *out = stdout;
  if (argc > 2) {
    out = fopen(argv[2], "w");
    if (out == NULL) return 1;
  }

  char text[256];
  size_t copied = 0;
  while (copied < (size_t)buf.st_size) {
    size_t nread = fread(text, 1, sizeof(text), f);
    size_t nwritten = fwrite(text, 1, nread, out);
    while (nwritten < nread)
      nwritten += fwrite(text + nwritten, 1, nread - nwritten, out);
    copied += nread;
  }

  fclose(f);
  fclose(out);

  return 0;
}
