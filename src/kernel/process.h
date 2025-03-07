
// process.h
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _PROCESS_H_
#define _PROCESS_H_

#include "../common/stdint.h"
#include "ds.h"
#include "fs.h"
#include "interrupt.h"

#define MAX_PROCESS_COUNT 64
#define MAX_PROCESS_PRIORITY 2
#define MAX_PROCESS_FDS 16
#define PROCESS_ENV_VADDR (KERNEL_START_VADDR - PAGE_SIZE)

// Registers struct to save the state of a process.
// The order of fields is important -- see process.s.
struct process_registers_s
{
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
typedef struct process_mmap_s
{
  uint32_t text;
  uint32_t data;
  uint32_t heap;
  uint32_t stack_top;
  uint32_t stack_bottom;
  uint32_t kernel_stack_top;
  uint32_t kernel_stack_bottom;
} process_mmap_t;

// Process image structs used to load binaries.
typedef struct process_image_s
{
  uint32_t entry;
  uint8_t *text;
  uint32_t text_len;
  uint32_t text_vaddr;
  uint8_t *data;
  uint32_t data_len;
  uint32_t data_vaddr;
} process_image_t;

// File descriptor.
typedef struct process_fd_s
{
  // TODO protect offset and refcount with a lock
  fs_node_t node;
  uint32_t offset;
  uint32_t refcount;
} process_fd_t;

// Process structure.
typedef struct process_s
{
  uint32_t pid;
  uint32_t gid;
  uint8_t is_thread;

  char *wd;
  process_fd_t *fds[MAX_PROCESS_FDS];
  volatile uint32_t fd_lock;

  uint8_t priority;
  uint8_t in_kernel;
  process_registers_t uregs;
  process_registers_t kregs;
  uint8_t fpregs[512];
  uint32_t thread_start;

  uint32_t cr3;
  process_mmap_t mmap;

  uint32_t next_signal;
  uint32_t current_signal;
  uint32_t signal_eip;
  uint8_t exited;
  uint32_t exit_status;
  process_registers_t saved_signal_regs;

  uint8_t has_ui;

  list_node_t *list_node;
} process_t;

// Process status structs used by waitpid
typedef struct
{
  uint32_t parent_pid;
  list_t waiters; // list of pids
  process_t *process;
  volatile uint32_t lock;
} process_status_t;

// Initialize the scheduler and other things.
uint32_t process_init();

// Add a process to the sleep queue.
uint32_t process_sleep(process_t *, uint64_t);

// Wait for a process to exit.
uint8_t process_wait_pid(process_t *p, uint32_t pid);

// Send a signal to a process.
uint8_t process_signal_pid(uint32_t pid, uint32_t signum);

// Get current process.
process_t *process_current();

// Switch to next scheduled process.
uint32_t process_switch_next();

// Update registers of current process.
void update_current_process_registers(cpu_state_t, stack_state_t);

// Implemented in process.s.
void resume_user();

// Create and schedule the `init` process.
uint32_t process_create_schedule_init(process_image_t);

// Overwrite process image.
uint32_t process_load(process_t *, process_image_t);

// Fork a process.
uint32_t process_fork(process_t *, process_t *, uint8_t);

// Add a process to the scheduler queue.
void process_schedule(process_t *);

// Remove a process from the scheduler queue.
void process_unschedule(process_t *);

// Kill a process.
void process_kill(process_t *);

#endif /* _PROCESS_H_ */
