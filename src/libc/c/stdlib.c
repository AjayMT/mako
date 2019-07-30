
// stdlib.c
//
// Standard library functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <_syscall.h>
#include <signal.h>
#include <stdlib.h>

static const uint32_t DEFAULT_ENVIRON = 0xBFFFF800;

char **environ = (char **)DEFAULT_ENVIRON;

char *getenv(const char *name)
{
  for (uint32_t i = 0; environ[i]; ++i) {
    if (environ[i] == NULL) continue;
    char *eq = strchr(environ[i], '=');
    if (strncmp(environ[i], name, eq - environ[i]) == 0)
      return eq + 1;
  }
  return NULL;
}

int32_t setenv(const char *name, const char *value, int32_t overwrite)
{
  int32_t p = -1;
  for (uint32_t i = 0; environ[i]; ++i) {
    if (environ[i] == NULL) continue;
    char *eq = strchr(environ[i], '=');
    if (strncmp(environ[i], name, eq - environ[i]) == 0) {
      p = i;
      break;
    }
  }

  if (p == -1) {
    uint32_t envc = 0;
    for (; environ[envc]; ++envc);
    char **newenviron = malloc((envc + 2) * sizeof(char *));
    for (uint32_t i = 0; i < envc; ++i)
      newenviron[i] = environ[i];
    newenviron[envc] = NULL;
    newenviron[envc + 1] = NULL;
    p = envc;

    if ((uint32_t)environ != DEFAULT_ENVIRON) free(environ);
    environ = newenviron;
  } else if (overwrite == 0) return 0;

  char *buf = malloc(strlen(name) + strlen(value) + 2);
  strcpy(buf, name);
  buf[strlen(name)] = '=';
  strcpy(buf + strlen(name) + 1, value);
  buf[strlen(name) + 1 + strlen(value)] = '\0';

  if ((uint32_t)(environ[p]) < DEFAULT_ENVIRON) free(environ[p]);
  environ[p] = buf;

  return 0;
}

int32_t unsetenv(const char *name)
{
  int32_t p = -1;
  for (uint32_t i = 0; environ[i]; ++i) {
    char *eq = strchr(environ[i], '=');
    if (strncmp(environ[i], name, eq - environ[i]) == 0) {
      p = i;
      break;
    }
  }
  if (p == -1) return 0;
  if ((uint32_t)(environ[p]) < DEFAULT_ENVIRON) free(environ[p]);
  environ[p] = NULL;
  return 0;
}

int32_t atexit(void *f)
{ return 0; }
void _fini();
void exit(int32_t status)
{ _fini(); _syscall1(SYSCALL_EXIT, (uint32_t)status); }
void abort()
{ raise(SIGABRT); }

// TODO
int32_t system(const char *command)
{ return -1; }

int64_t labs(int64_t i)
{ return i < 0 ? -i : i; }

void *calloc(size_t n, size_t s)
{
  char *p = malloc(n * s);
  if (p == NULL) return NULL;
  memset(p, 0, n * s);
  return p;
}

// TODO real rand
static uint32_t rand_seed = 0x420;
int32_t rand()
{
  rand_seed = (rand_seed + 1) % RAND_MAX;
  return abs(rand_seed);
}
void srand(uint32_t seed)
{ rand_seed = seed; }
