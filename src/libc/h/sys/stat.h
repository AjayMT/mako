
// stat.h
//
// Filesystem info.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _STAT_H_
#define _STAT_H_

#include <stdint.h>

struct stat {
  uint16_t st_dev;
  uint16_t st_ino;
  uint32_t st_mode;
  uint16_t st_nlink;
  uint16_t st_uid;
  uint16_t st_gid;
  uint16_t st_rdev;
  int32_t st_size;
  uint32_t st_atime;
  uint32_t __unused1;
  int32_t st_mtime;
  uint32_t __unused2;
  uint32_t st_ctime;
  uint32_t __unused3;
  uint32_t st_blksize;
  uint32_t st_blocks;
};

int32_t stat(const char *file, struct stat *st);
int32_t lstat(const char *path, struct stat *st);
int32_t fstat(const uint32_t fd, struct stat *st);
int32_t mkdir(const char *path, int32_t mode);

#endif /* _STAT_H_ */
