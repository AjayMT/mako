
// rd.h
//
// Ramdisk filesystem.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _RD_H_
#define _RD_H_

#include <stdint.h>
#include <fs/fs.h>

// Initialize the ramdisk. Takes the location of the loaded
// GRUB module.
uint32_t rd_init(const uint32_t, const uint32_t);

// Filesystem operations.
void rd_open(fs_node_t *, uint32_t);
void rd_close(fs_node_t *);
uint32_t rd_read(fs_node_t *, uint32_t, uint32_t, uint8_t *);
uint32_t rd_write(fs_node_t *, uint32_t, uint32_t, uint8_t *);
struct dirent *rd_readdir(fs_node_t *, uint32_t);
fs_node_t *rd_finddir(fs_node_t *, char *);

#endif /* _RD_H_ */
