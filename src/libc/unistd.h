
// unistd.h
//
// Some syscalls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _UNISTD_H_
#define _UNISTD_H_

#include "stdint.h"
#include <stddef.h>
#include "sys/types.h"

pid_t getpid();
int32_t close(uint32_t fd);
pid_t fork();
int32_t execve(const char *path, char *const argv[], char *const envp[]);
int32_t execv(const char *path, char *const argv[]);
int32_t execvp(const char *path, char *const argv[]);
char *getcwd(char *buf, size_t size);
size_t write(uint32_t fd, const void *buf, size_t count);
size_t read(uint32_t fd, const void *buf, size_t count);
int32_t symlink(const char *target, const char *linkpath);
size_t readlink(const char *pathname, char *buf, size_t bufsize);
int32_t chdir(const char *path);
int32_t isatty(uint32_t fd);
off_t lseek(uint32_t fd, off_t offset, int32_t whence);
int32_t unlink(const char *path);
int32_t rmdir(const char *path);
int32_t dup(uint32_t fd);

#endif /* _UNISTD_H_ */
