
// libgen.c
//
// basename and dirname.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "string.h"
#include "sys/param.h"
#include "stdint.h"
#include "errno.h"
#include "libgen.h"

static char gpath[MAXPATHLEN];

char *dirname(char *path)
{
  if (path == NULL || strchr(path, '/') == NULL) {
    strcpy(gpath, "."); return gpath;
  }

  char *slash = strrchr(path, '/');
  while (slash > path && *(slash - 1) == '/') --slash;
  if (slash == path) ++slash;
  memcpy(gpath, path, slash - path);
  gpath[slash - path] = '\0';
  return gpath;
}

char *basename(char *path)
{
  uint32_t pathlen = 0;
  if (path == NULL || (pathlen = strlen(path)) == 0) {
  root: strcpy(gpath, "/"); return gpath;
  }

  while (pathlen && path[pathlen - 1] == '/') --pathlen;
  if (pathlen == 0) goto root;

  char *end = path + pathlen;
  char *slash = strrchr(path, '/');
  if (slash) path = slash + 1;

  memcpy(gpath, path, end - path);
  gpath[end - path] = '\0';
  return gpath;
}
