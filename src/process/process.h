
// process.h
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <stdint.h>
#include <ds/ds.h>

#define PROCESS_NAME_LEN 256

// Registers struct to save the state of a process.
// The order of fields is important -- see process.s.
struct process_registers_s {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint32_t ss;
  uint32_t esp;
  uint32_t eflags;
  uint32_t cs;
  uint32_t eip;
} __attribute__((packed));
typedef struct process_registers_s process_registers_t;

// Mapped memory regions associated with each process.
typedef struct process_mmap_s {
  uint32_t text;
  uint32_t data;
  uint32_t stack_top;
  uint32_t stack_bottom;
  uint32_t kernel_stack_top;
  uint32_t kernel_stack_bottom;
} process_mmap_t;

// Process structure.
typedef struct process_s {
  uint32_t pid;
  uint32_t gid;
  char name[PROCESS_NAME_LEN];

  uint8_t is_running;
  uint8_t is_thread;
  uint8_t is_finished;
  process_registers_t regs;

  uint32_t brk;
  uint32_t cr3;
  process_mmap_t mmap;

  tree_node_t *tree_node;
  list_node_t *list_node;
} process_t;

// Initialize the scheduler and other things.
void process_init();

// Create the `init` process.
process_t *process_create_init(
  uint8_t *, uint32_t, uint8_t *, uint32_t
  );

// Overwrite process image.
void process_load(
  process_t *, uint8_t *, uint32_t, uint8_t *, uint32_t
  );

// Fork a process.
process_t *process_fork(process_t *);

// Add a process to the scheduler queue.
void process_schedule(process_t *);

// Mark a process as finished, deal with children.
void process_finish(process_t *);

// Destroy a process.
void process_destroy(process_t *);

#endif /* _PROCESS_H_ */
