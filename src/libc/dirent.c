
// dirent.c
//
// Directory functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "dirent.h"
#include "_syscall.h"
#include "errno.h"
#include "fcntl.h"
#include "stdint.h"
#include "stdlib.h"
#include "unistd.h"

DIR *opendir(char *path)
{
  int32_t res = open(path, O_RDONLY | O_DIRECTORY);
  if (res == -1)
    return NULL;
  DIR *d = malloc(sizeof(DIR));
  d->fd = res;
  d->current_entry = 0;
  return d;
}

int32_t closedir(DIR *d)
{
  int32_t res = close(d->fd);
  free(d);
  return res;
}

struct dirent *readdir(DIR *d)
{
  struct dirent *ent = malloc(sizeof(struct dirent));
  int32_t res = _syscall3(SYSCALL_READDIR, d->fd, (uint32_t)ent, d->current_entry);
  if (res < 0) {
    errno = -res;
    free(ent);
    return NULL;
  }
  ++(d->current_entry);
  return ent;
}
