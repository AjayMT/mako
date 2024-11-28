
// wait.h
//
// Process wait functions.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _WAIT_H_
#define _WAIT_H_

#include "../stdint.h"
#include "types.h"

#define WIFEXITED(w) ((w) & 1)
#define WEXITSTATUS(w) (((w) >> 1) & 0x7fff)
#define WTERMSIG(w) ((w) >> 16)
#define WIFSIGNALED(w) (WTERMSIG(w) != 0)

int32_t waitpid(pid_t pid, int32_t *stat_loc, int32_t options);

#endif /* _WAIT_H_ */
