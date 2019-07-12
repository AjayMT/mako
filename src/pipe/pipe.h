
// pipe.h
//
// Unix-style pipe for IPC.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PIPE_H_
#define _PIPE_H_

#include <stdint.h>
#include <fs/fs.h>

uint32_t pipe_create(fs_node_t *read_node, fs_node_t *write_node);

#endif /* _PIPE_H_ */
