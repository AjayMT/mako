
// process.h
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <stdint.h>
#include <interrupt/interrupt.h>
#include <fs/fs.h>
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
  uint32_t heap;
  uint32_t stack_top;
  uint32_t stack_bottom;
  uint32_t kernel_stack_top;
  uint32_t kernel_stack_bottom;
} process_mmap_t;

// Process image structs used to load binaries.
typedef struct process_image_s {
  uint32_t entry;
  uint8_t *text;
  uint32_t text_len;
  uint32_t text_vaddr;
  uint8_t *data;
  uint32_t data_len;
  uint32_t data_vaddr;
} process_image_t;

// File descriptor.
typedef struct process_fd_s {
  fs_node_t node;
  uint32_t offset;
  uint32_t is_pipe;
  uint32_t refcount;
} process_fd_t;

// Process structure.
typedef struct process_s {
  uint32_t pid;
  uint32_t gid;
  char name[PROCESS_NAME_LEN];
  char *wd;
  list_t *fds;

  uint8_t is_running;
  uint8_t is_thread;
  uint8_t is_finished;
  uint8_t in_kernel;
  process_registers_t uregs;
  process_registers_t kregs;
  uint8_t fpregs[512];
  uint32_t thread_start;

  uint32_t cr3;
  process_mmap_t mmap;

  uint32_t signal_pending;
  uint32_t next_signal;
  uint32_t signal_eip;
  uint8_t exited;
  uint32_t exit_status;
  process_registers_t saved_signal_regs;

  uint32_t ui_eip;
  uint32_t ui_event_buffer;
  list_t *ui_event_queue;
  uint8_t ui_event_pending;
  process_registers_t saved_ui_regs;

  tree_node_t *tree_node;
  list_node_t *list_node;
} process_t;

// A node in the sleep queue.
typedef struct process_sleep_node_s {
  uint32_t wake_time;
  process_t *process;
} process_sleep_node_t;

// Initialize the scheduler and other things.
uint32_t process_init();

// Find a process with a specific PID.
process_t *process_from_pid(uint32_t);

// Add a process to the sleep queue.
uint32_t process_sleep(process_t *, uint32_t);

// Send a signal to a process.
void process_signal(process_t *, uint32_t);

// Get current process.
process_t *process_current();

// Switch to next scheduled process.
uint32_t process_switch_next();

// Update registers of current process.
void update_current_process_registers(cpu_state_t, stack_state_t);

// Implemented in process.s.
void enter_usermode();

// Switch processes.
void process_switch(process_t *);

// Create the `init` process.
uint32_t process_create_init(process_t *, process_image_t);

// Overwrite process image.
uint32_t process_load(process_t *, process_image_t);

// Fork a process.
uint32_t process_fork(process_t *, process_t *, uint8_t);

// Set a process's argv and envp.
uint32_t process_set_env(process_t *p, char *argv[], char *envp[]);

// Add a process to the scheduler queue.
void process_schedule(process_t *);

// Mark a process as finished, deal with children.
void process_finish(process_t *);

#endif /* _PROCESS_H_ */
