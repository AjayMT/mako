
// unistd.c
//
// Some syscalls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "stdint.h"
#include <stddef.h>
#include "sys/types.h"
#include "sys/stat.h"
#include "stdlib.h"
#include "errno.h"
#include "_syscall.h"
#include "unistd.h"

pid_t getpid()
{
  int32_t res = _syscall0(SYSCALL_GETPID);
  return (pid_t)res;
}

int32_t close(uint32_t fd)
{
  int32_t res = _syscall1(SYSCALL_CLOSE, fd);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

pid_t fork()
{
  int32_t res = _syscall0(SYSCALL_FORK);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t execve(const char *path, char *const argv[], char *const envp[])
{
  char *largv[] = { NULL };
  char *lenvp[] = { NULL };
  if (argv == NULL) argv = largv;
  if (envp == NULL) envp = largv;
  int32_t res = _syscall3(
    SYSCALL_EXECVE, (uint32_t)path, (uint32_t)argv, (uint32_t)envp
    );
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t execv(const char *path, char *const argv[])
{ return execve(path, argv, environ); }
int32_t execvp(const char *path, char *const argv[])
{ return execve(path, argv, environ); }

char *getcwd(char *buf, size_t size)
{
  if (buf == NULL) {
    buf = malloc(1024);
    size = 1024;
  }
  _syscall2(SYSCALL_GETCWD, (uint32_t)buf, size);
  return buf;
}

size_t write(uint32_t fd, const void *buf, size_t count)
{
  int32_t res = _syscall3(SYSCALL_WRITE, fd, (uint32_t)buf, count);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

size_t read(uint32_t fd, const void *buf, size_t count)
{
  int32_t res = _syscall3(SYSCALL_READ, fd, (uint32_t)buf, count);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t symlink(const char *target, const char *linkpath)
{
  int32_t res = _syscall2(
    SYSCALL_SYMLINK, (uint32_t)target, (uint32_t)linkpath
    );
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

size_t readlink(const char *path, char *buf, size_t bufsize)
{
  int32_t res = _syscall3(
    SYSCALL_READLINK, (uint32_t)path, (uint32_t)buf, bufsize
    );
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t chdir(const char *path)
{
  int32_t res = _syscall1(SYSCALL_CHDIR, (uint32_t)path);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t isatty(uint32_t fd)
{
  struct stat st;
  int32_t res = fstat(fd, &st);
  if (res == -1) return 0;
  return st.st_dev & 0x80;
}

off_t lseek(uint32_t fd, off_t offset, int32_t whence)
{
  int32_t res = _syscall3(
    SYSCALL_LSEEK, fd, (uint32_t)offset, (uint32_t)whence
    );
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t unlink(const char *path)
{
  int32_t res = _syscall1(SYSCALL_UNLINK, (uint32_t)path);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}

int32_t rmdir(const char *path)
{ return unlink(path); }

int32_t dup(uint32_t fd)
{
  int32_t res = _syscall1(SYSCALL_DUP, fd);
  if (res < 0) { errno = -res; res = -1; }
  return res;
}
