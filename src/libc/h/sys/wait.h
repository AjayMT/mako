
// wait.h
//
// Process wait functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _WAIT_H_
#define _WAIT_H_

#include <stdint.h>
#include <sys/types.h>

struct _wait_result {
  uint32_t exited : 1;
  uint32_t status : 16;
  uint32_t signal : 15;
} __attribute__((packed));

#define WIFEXITED(w)   ((_wait_result *)(&(w))->exited)
#define WEXITSTATUS(w) ((_wait_result *)(&(w))->status)
#define WIFSIGNALED(w) ((_wait_result *)(&(w))->exited == 0)
#define WTERMSIG(w)    ((_wait_result *)(&(w))->signal)

int32_t waitpid(pid_t pid, int32_t *stat_loc, int32_t options);

#endif /* _WAIT_H_ */
