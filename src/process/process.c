
// process.c
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <interrupt/interrupt.h>
#include <pit/pit.h>
#include <tss/tss.h>
#include <ds/ds.h>
#include <kheap/kheap.h>
#include <pmm/pmm.h>
#include <paging/paging.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include "process.h"

// Constants.
static const uint32_t SCHEDULER_INTERVAL   = 2;
static const uint32_t SCHEDULER_TIME_SLICE = 10 * SCHEDULER_INTERVAL;
static const uint32_t USER_MODE_CS         = 0x18;
static const uint32_t USER_MODE_DS         = 0x20;

static inline uint32_t page_align_up(uint32_t addr)
{
  if (addr != (addr & 0xFFFFF000))
    addr = (addr & 0xFFFFF000) + PAGE_SIZE;
  return addr;
}

static uint32_t next_pid = 0;
static process_t *init_process = NULL;
static process_t *current_process = NULL;
static tree_node_t *process_tree = NULL;
static list_t *process_queue = NULL;

// Implemented in process.s.
void enter_usermode(process_registers_t *);

// Save the registers of the current process.
static void update_current_process_registers(
  cpu_state_t cstate, stack_state_t sstate
  )
{
  current_process->regs.edi = cstate.edi;
  current_process->regs.esi = cstate.esi;
  current_process->regs.ebp = cstate.ebp;
  current_process->regs.edx = cstate.edx;
  current_process->regs.ecx = cstate.ecx;
  current_process->regs.ebx = cstate.ebx;
  current_process->regs.eax = cstate.eax;
  current_process->regs.eip = sstate.eip;
  current_process->regs.cs = sstate.cs;
  current_process->regs.eflags = sstate.eflags;

  if (sstate.cs == (USER_MODE_CS | 3)) {
    current_process->regs.esp = sstate.user_esp;
    current_process->regs.ss = sstate.user_ss;
  } else {
    current_process->regs.esp = cstate.esp + 12;
    current_process->regs.ss = SEGMENT_SELECTOR_KERNEL_CS;
  }
}

// Switch processes.
static void process_switch(process_t *process)
{
  current_process = process;
  process->regs.cs = USER_MODE_CS | 3;
  process->regs.ss = USER_MODE_DS | 3;

  tss_set_kernel_stack(
    SEGMENT_SELECTOR_KERNEL_DS, process->mmap.kernel_stack
    );
  paging_init(process->pd, process->cr3);
  enter_usermode(&(process->regs));
}

// Interrupt handler that switches processes.
static void scheduler_interrupt_handler(
  cpu_state_t cstate, idt_info_t info, stack_state_t sstate
  )
{
  disable_interrupts();

  static uint32_t ms = 0;
  ms = (ms + SCHEDULER_INTERVAL) % SCHEDULER_TIME_SLICE;
  if (ms) return;
  if (process_queue == NULL || process_queue->size == 0) return;

  if (current_process)
    update_current_process_registers(cstate, sstate);
  else current_process = process_queue->head->value;

  process_t *next = current_process;
  do {
    list_node_t *node = next->queue_node->next;
    if (node == NULL) node = process_queue->head;
    next = node->value;
  } while (next->is_running == 0 && next != current_process);

  paging_copy_kernel_space(next->pd);
  process_switch(next);
}

// Initialize the scheduler and other things.
void process_init()
{
  process_tree = tree_init(NULL);
  process_queue = kmalloc(sizeof(list_t));
  u_memset(process_queue, 0, sizeof(list_t));

  pit_set_interval(SCHEDULER_INTERVAL);
  register_interrupt_handler(32, scheduler_interrupt_handler);
}

// Create the `init` process.
process_t *process_create_init(
  uint8_t *text, uint32_t text_len,
  uint8_t *data, uint32_t data_len
  )
{
  uint32_t eflags = interrupt_save_disable();

  process_t *init = kmalloc(sizeof(process_t));
  u_memset(init, 0, sizeof(process_t));
  u_memcpy(init->name, "init", 5);
  init->is_running = 1;

  page_directory_t kernel_pd; uint32_t kernel_cr3;
  paging_get_kernel_pd(&kernel_pd, &kernel_cr3);
  paging_clone_process_directory(kernel_pd, &(init->pd), &(init->cr3));

  uint32_t kstack_vaddr = paging_prev_vaddr(1, PD_VADDR);
  uint32_t kstack_paddr = pmm_alloc(1);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_map(kstack_vaddr, kstack_paddr, flags);
  init->mmap.kernel_stack = kstack_vaddr + PAGE_SIZE - 1;

  process_load(init, text, text_len, data, data_len);
  process_tree->value = init;
  init->tree_node = process_tree;
  init->regs.cs = USER_MODE_CS | 3;
  init->regs.ss = USER_MODE_DS | 3;
  init->regs.eflags = 0x202;
  init_process = init;

  interrupt_restore(eflags);
  return init;
}

// Fork a process.
process_t *process_fork(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();

  process_t *child = kmalloc(sizeof(process_t));
  u_memcpy(child, process, sizeof(process_t));
  child->pid = ++next_pid;
  tree_node_t *node = tree_init(child);
  child->tree_node = node;
  tree_insert(process->tree_node, node);
  child->queue_node = NULL;

  uint32_t kstack_vaddr = paging_prev_vaddr(1, PD_VADDR);
  uint32_t kstack_paddr = pmm_alloc(1);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_map(kstack_vaddr, kstack_paddr, flags);
  child->mmap.kernel_stack = kstack_vaddr + PAGE_SIZE - 1;

  paging_clone_process_directory(process->pd, &(child->pd), &(child->cr3));

  interrupt_restore(eflags);
  return child;
}

// Overwrite a process image.
void process_load(
  process_t *process,
  uint8_t *text, uint32_t text_len,
  uint8_t *data, uint32_t data_len
  )
{
  if (text_len == 0) return;

  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();

  paging_init(process->pd, process->cr3);
  paging_clear_user_space();

  uint32_t npages = page_align_up(text_len) >> PHYS_ADDR_OFFSET;
  uint32_t text_vaddr = paging_next_vaddr(npages, 0);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.user = 1;
  flags.rw = 1;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    paging_map(text_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags);
  }

  u_memcpy((uint8_t *)text_vaddr, text, text_len);

  uint32_t data_vaddr = text_vaddr + page_align_up(text_len);
  if (data_len) {
    npages = page_align_up(data_len) >> PHYS_ADDR_OFFSET;
    data_vaddr = paging_next_vaddr(npages, 0); // Should be the same as before.
    for (uint32_t i = 0; i < npages; ++i) {
      uint32_t paddr = pmm_alloc(1);
      paging_map(data_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags);
    }

    u_memcpy((uint8_t *)data_vaddr, data, data_len);
  }

  uint32_t stack_paddr = pmm_alloc(1);
  uint32_t stack_vaddr = paging_prev_vaddr(1, KERNEL_START_VADDR);
  flags.user = 0;
  paging_map(stack_vaddr, stack_paddr, flags);

  paging_set_directory(cr3);

  process->mmap.text = text_vaddr;
  process->mmap.stack = stack_vaddr + PAGE_SIZE - 1;
  process->mmap.data = data_vaddr;
  process->brk = process->mmap.data;
  process->regs.eip = process->mmap.text;
  process->regs.ebp = process->mmap.stack;
  process->regs.esp = process->mmap.stack;

  interrupt_restore(eflags);
}

// Add a process to the scheduler queue.
void process_schedule(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();
  list_push_back(process_queue, process);
  process->queue_node = process_queue->tail;
  interrupt_restore(eflags);
}
