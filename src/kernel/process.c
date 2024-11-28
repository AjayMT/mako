
// process.c
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "process.h"
#include "../common/errno.h"
#include "../common/signal.h"
#include "../common/stdint.h"
#include "constants.h"
#include "ds.h"
#include "fpu.h"
#include "fs.h"
#include "interrupt.h"
#include "kheap.h"
#include "klock.h"
#include "log.h"
#include "paging.h"
#include "pipe.h"
#include "pit.h"
#include "pmm.h"
#include "tss.h"
#include "ui.h"
#include "util.h"
#include <stddef.h>

#define CHECK(err, msg, code)                                                                      \
  if ((err)) {                                                                                     \
    log_error("process", msg "\n");                                                                \
    return (code);                                                                                 \
  }

static const uint32_t USER_MODE_CS = 0x18;
static const uint32_t USER_MODE_DS = 0x20;
static const uint32_t ENV_VADDR = KERNEL_START_VADDR - PAGE_SIZE;

// Process tree and process status state.
static process_t *init_process = NULL;
static process_t *current_process = NULL;
static heap_t sleep_queue;
static process_status_t pids[MAX_PROCESS_COUNT];

// Scheduler queues.
static list_t running_lists[MAX_PROCESS_PRIORITY + 1];

// Free list of pages used for process kernel stacks.
static list_t kernel_stack_pages;

// Implemented in process.s.
void resume_kernel(process_registers_t *);

// Save the registers of the current process.
void update_current_process_registers(cpu_state_t cstate, stack_state_t sstate)
{
  process_registers_t *regs;
  if (sstate.cs == (USER_MODE_CS | 3)) {
    current_process->in_kernel = 0;
    regs = &(current_process->uregs);
    regs->ss = sstate.user_ss;
    regs->esp = sstate.user_esp;
  } else {
    current_process->in_kernel = 1;
    regs = &(current_process->kregs);
    regs->ss = SEGMENT_SELECTOR_KERNEL_DS;
    regs->esp = cstate.esp + 20;
  }

  regs->edi = cstate.edi;
  regs->esi = cstate.esi;
  regs->ebp = cstate.ebp;
  regs->edx = cstate.edx;
  regs->ecx = cstate.ecx;
  regs->ebx = cstate.ebx;
  regs->eax = cstate.eax;
  regs->eip = sstate.eip;
  regs->cs = sstate.cs;
  regs->eflags = sstate.eflags | 0x202;
}

// Resume a running process.
static void process_resume(process_t *process)
{
  if (current_process)
    fpu_save(current_process);
  fpu_restore(process);
  current_process = process;
  tss_set_kernel_stack(SEGMENT_SELECTOR_KERNEL_DS, process->mmap.kernel_stack_top);
  paging_set_cr3(process->cr3);
  if (process->in_kernel)
    resume_kernel(&(process->kregs));
  else
    resume_user(&(process->uregs));
}

// Switch to next scheduled process.
uint32_t process_switch_next()
{
  uint32_t eflags = interrupt_save_disable();
  list_t *running_list;
  for (int32_t i = MAX_PROCESS_PRIORITY; i >= 0; --i) {
    running_list = &running_lists[i];
    if (running_list->size)
      break;
  }
  if (running_list->size == 0) {
    interrupt_restore(eflags);
    return 1;
  }

  process_t *next = running_list->head->value;
  uint32_t res = paging_copy_kernel_space(next->cr3);
  if (res) {
    interrupt_restore(eflags);
    log_error("process", "Failed to copy kernel address space.");
    return res;
  }

  // Rotate the queue
  list_remove(running_list, next->list_node, 0);
  next->list_node->prev = running_list->tail;
  next->list_node->next = NULL;
  if (running_list->tail)
    running_list->tail->next = next->list_node;
  running_list->tail = next->list_node;
  if (running_list->head == NULL)
    running_list->head = next->list_node;
  running_list->size++;

  if (next->in_kernel) {
    process_resume(next);
    return 0;
  }

  // Jump to signal handler if necessary
  if (next->next_signal && next->current_signal == 0) {
    u_memcpy(&(next->saved_signal_regs), &(next->uregs), sizeof(process_registers_t));
    next->current_signal = next->next_signal;
    next->next_signal = 0;
    next->uregs.eip = next->signal_eip;
    next->uregs.edi = next->current_signal;
  }

  process_resume(next);

  return 0;
}

// General protection fault handler.
static void gp_fault_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  log_error("process", "eip %x: gpf %x cs %x\n", ss.eip, info.error_code, ss.cs);
  current_process->next_signal = SIGILL;
  process_kill(current_process);
  process_switch_next();
}

// Page fault handler.
static void page_fault_handler(cpu_state_t cs, idt_info_t info, stack_state_t ss)
{
  uint32_t vaddr;
  asm("movl %%cr2, %0" : "=r"(vaddr));
  log_error("process",
            "eip %x: page fault %x vaddr %x esp %x pid %u\n",
            ss.eip,
            info.error_code,
            vaddr,
            ss.user_esp,
            current_process->pid);

  if (ss.cs == (USER_MODE_CS | 3)) {
    uint32_t stb = current_process->mmap.stack_bottom;
    if (vaddr < stb && stb - vaddr < PAGE_SIZE) {
      uint32_t paddr = pmm_alloc(1);
      if (paddr == 0)
        goto die;
      uint32_t vaddr = stb - PAGE_SIZE;
      page_table_entry_t flags;
      u_memset(&flags, 0, sizeof(flags));
      flags.rw = 1;
      flags.user = 1;
      paging_result_t res = paging_map(vaddr, paddr, flags);
      if (res != PAGING_OK)
        goto die;
      current_process->mmap.stack_bottom = vaddr;
      log_info("process", "grew the stack to %x for process %u\n", vaddr, current_process->pid);
      return;
    }

    current_process->next_signal = SIGSEGV;
    process_switch_next();
    return;
  }

  // Yikes, kernel page fault.
die:
  current_process->next_signal = SIGSEGV;
  process_kill(current_process);
  process_switch_next();
}

// Interrupt handler that switches processes.
static void scheduler_interrupt_handler(cpu_state_t cstate, idt_info_t info, stack_state_t sstate)
{
  uint32_t eflags = interrupt_save_disable();

  if (current_process)
    update_current_process_registers(cstate, sstate);

  uint64_t current_time = pit_get_time();
  while (sleep_queue.size && current_time >= heap_peek(&sleep_queue)->key) {
    uint32_t pid = (uint32_t)heap_pop(&sleep_queue).value;
    if (pids[pid].process)
      process_schedule(pids[pid].process);
  }

  process_switch_next();
  interrupt_restore(eflags);
}

// Initialize the scheduler and other things.
uint32_t process_init()
{
  for (uint32_t i = 0; i <= MAX_PROCESS_PRIORITY; ++i)
    u_memset(&running_lists[i], 0, sizeof(list_t));

  u_memset(&sleep_queue, 0, sizeof(heap_t));
  u_memset(pids, 0, sizeof(pids));
  u_memset(&kernel_stack_pages, 0, sizeof(list_t));

  pit_set_handler(scheduler_interrupt_handler);
  register_interrupt_handler(13, gp_fault_handler);
  register_interrupt_handler(14, page_fault_handler);

  return 0;
}

// Add a process to the sleep queue.
uint32_t process_sleep(process_t *p, uint64_t wake_time)
{
  uint32_t eflags = interrupt_save_disable();
  heap_push(&sleep_queue, wake_time, (void *)p->pid);
  interrupt_restore(eflags);
  return 0;
}

// Wait for a process to exit.
uint8_t process_wait_pid(process_t *p, uint32_t pid)
{
  if (pid >= MAX_PROCESS_COUNT)
    return 1;
  klock(&pids[pid].lock);
  if (pids[pid].process == NULL) {
    kunlock(&pids[pid].lock);
    return 1;
  }
  list_push_back(&pids[pid].waiters, (void *)p->pid);
  kunlock(&pids[pid].lock);
  return 0;
}

// Send a signal to a process.
uint8_t process_signal_pid(uint32_t pid, uint32_t signum)
{
  if (signum == 0)
    return 1;
  if (pid >= MAX_PROCESS_COUNT)
    return 1;
  klock(&pids[pid].lock);
  if (pids[pid].process == NULL) {
    kunlock(&pids[pid].lock);
    return 1;
  }
  pids[pid].process->next_signal = signum;
  if (pids[pid].process->signal_eip == 0 || signum == SIGKILL)
    process_kill(pids[pid].process);
  kunlock(&pids[pid].lock);
  return 0;
}

// Get current process.
process_t *process_current()
{
  return current_process;
}

#define CHECK_RESTORE_EFLAGS(err, msg, code)                                                       \
  if ((err)) {                                                                                     \
    log_error("process", msg "\n");                                                                \
    interrupt_restore(eflags);                                                                     \
    return (code);                                                                                 \
  }

uint32_t kernel_stack_page_alloc()
{
  uint32_t eflags = interrupt_save_disable();

  if (kernel_stack_pages.size > 0) {
    list_node_t *head = kernel_stack_pages.head;
    list_remove(&kernel_stack_pages, head, 0);
    uint32_t addr = (uint32_t)head->value;
    kfree(head);
    interrupt_restore(eflags);
    return addr;
  }

  uint32_t kstack_vaddr = paging_prev_vaddr(1, FIRST_PT_VADDR);
  CHECK_RESTORE_EFLAGS(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1);
  CHECK_RESTORE_EFLAGS(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK_RESTORE_EFLAGS(res != PAGING_OK, "Failed to map kernel stack.", res);

  interrupt_restore(eflags);
  return kstack_vaddr;
}

void kernel_stack_page_free(uint32_t addr)
{
  uint32_t eflags = interrupt_save_disable();
  list_push_front(&kernel_stack_pages, (void *)addr);
  interrupt_restore(eflags);
}

// Create the `init` process.
uint32_t process_create_schedule_init(process_image_t img)
{
  process_t *init = kmalloc(sizeof(process_t));
  CHECK(init == NULL, "No memory.", ENOMEM);
  pids[0].process = init;
  u_memset(init, 0, sizeof(process_t));
  init->wd = kmalloc(2);
  CHECK(init->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(init->wd, "/", u_strlen("/") + 1);

  page_directory_t kernel_pd;
  uint32_t kernel_cr3;
  paging_get_kernel_pd(&kernel_pd, &kernel_cr3);
  uint32_t err = paging_clone_process_directory(&(init->cr3), kernel_cr3);
  CHECK(err, "Failed to clone page directory.", err);

  uint32_t kstack_vaddr = kernel_stack_page_alloc();
  init->mmap.kernel_stack_bottom = kstack_vaddr;
  init->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 8;

  err = process_load(init, img);
  CHECK(err, "Failed to load process image.", err);
  init->uregs.cs = USER_MODE_CS | 3;
  init->uregs.ss = USER_MODE_DS | 3;
  init->uregs.eflags = 0x202;

  init_process = init;
  process_schedule(init);

  return 0;
}

// Allocate a PID and acquire its lock.
static uint32_t alloc_pid()
{
  for (uint32_t i = 0; i < MAX_PROCESS_COUNT; ++i) {
    klock(&pids[i].lock);
    if (pids[i].process || pids[i].waiters.size) {
      kunlock(&pids[i].lock);
      continue;
    }
    return i;
  }

  return 0;
}

#define CHECK_RESTORE_EFLAGS_CR3(err, msg, code)                                                   \
  if ((err)) {                                                                                     \
    log_error("process", msg "\n");                                                                \
    paging_set_cr3(cr3);                                                                           \
    interrupt_restore(eflags);                                                                     \
    return (code);                                                                                 \
  }

// Fork a process.
uint32_t process_fork(process_t *child, process_t *process, uint8_t is_thread)
{
  u_memcpy(child, process, sizeof(process_t));
  kunlock(&child->fd_lock);
  child->in_kernel = 0;
  child->is_thread = is_thread;
  child->wd = kmalloc(u_strlen(process->wd) + 1);
  CHECK(child->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(child->wd, process->wd, u_strlen(process->wd) + 1);
  child->pid = alloc_pid();
  CHECK(child->pid == 0, "Too many processes.", ENOMEM);
  pids[child->pid].process = child;
  pids[child->pid].parent_pid = process->pid;
  u_memset(&pids[child->pid].waiters, 0, sizeof(list_t));
  kunlock(&pids[child->pid].lock);
  child->gid = child->pid;
  child->list_node = NULL;
  child->has_ui = 0;

  if (is_thread) {
    child->gid = process->gid;
    uint32_t eflags = interrupt_save_disable();
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(process->cr3);

    uint32_t stack_vaddr = paging_prev_vaddr(1, process->mmap.text);
    CHECK_RESTORE_EFLAGS_CR3(
      stack_vaddr == 0, "Failed to allocate thread stack virtual page.", ENOMEM);
    uint32_t stack_paddr = pmm_alloc(1);
    CHECK_RESTORE_EFLAGS_CR3(
      stack_paddr == 0, "Failed to allocate thread stack physical page.", ENOMEM);

    page_table_entry_t flags;
    u_memset(&flags, 0, sizeof(flags));
    flags.rw = 1;
    flags.user = 1;
    paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
    CHECK_RESTORE_EFLAGS_CR3(res != PAGING_OK, "Failed to map thread stack page.", res);

    child->mmap.stack_top = stack_vaddr + PAGE_SIZE - 4;
    child->mmap.stack_bottom = stack_vaddr;

    paging_set_cr3(cr3);
    interrupt_restore(eflags);
  } else {
    uint32_t err = paging_clone_process_directory(&(child->cr3), process->cr3);
    CHECK(err, "Failed to clone page directory.", err);
  }

  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i)
    if (child->fds[i])
      child->fds[i]->refcount++;

  uint32_t kstack_vaddr = kernel_stack_page_alloc();
  child->mmap.kernel_stack_bottom = kstack_vaddr;
  child->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 8;

  return 0;
}

// Overwrite a process image.
uint32_t process_load(process_t *process, process_image_t img)
{
  CHECK(img.text_len == 0, "No text.", 1);

  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();

  paging_set_cr3(process->cr3);
  uint32_t err = paging_clear_user_space();
  CHECK_RESTORE_EFLAGS_CR3(err, "Failed to clear user address space.", err);

  uint32_t npages = u_page_align_up(img.text_len) >> PHYS_ADDR_OFFSET;
  page_table_entry_t flags;
  u_memset(&flags, 0, sizeof(flags));
  flags.user = 1;
  flags.rw = 1;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_RESTORE_EFLAGS_CR3(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res =
      paging_map((uint32_t)img.text_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags);
    CHECK_RESTORE_EFLAGS_CR3(res != PAGING_OK, "Failed to map text pages.", res);
  }

  u_memcpy((uint8_t *)img.text_vaddr, img.text, img.text_len);

  npages = u_page_align_up(img.data_len) >> PHYS_ADDR_OFFSET;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_RESTORE_EFLAGS_CR3(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res =
      paging_map((uint32_t)img.data_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags);
    CHECK_RESTORE_EFLAGS_CR3(res != PAGING_OK, "Failed to map data pages.", res);
  }

  u_memcpy((uint8_t *)img.data_vaddr, img.data, img.data_len);

  uint32_t stack_paddr = pmm_alloc(1);
  CHECK_RESTORE_EFLAGS_CR3(stack_paddr == 0, "No memory.", ENOMEM);
  // Leave one page between the stack and the kernel for environment variables.
  uint32_t stack_vaddr = paging_prev_vaddr(1, KERNEL_START_VADDR - PAGE_SIZE);
  CHECK_RESTORE_EFLAGS_CR3(stack_vaddr == 0, "No memory.", ENOMEM);
  paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
  CHECK_RESTORE_EFLAGS_CR3(res != PAGING_OK, "Failed to map stack pages.", res);

  uint32_t env_paddr = pmm_alloc(1);
  CHECK_RESTORE_EFLAGS_CR3(env_paddr == 0, "No memory.", ENOMEM);
  flags.user = 1;
  flags.rw = 1;
  res = paging_map(ENV_VADDR, env_paddr, flags);
  CHECK_RESTORE_EFLAGS_CR3(res != PAGING_OK, "Failed to map env page.", res);

  paging_set_cr3(cr3);

  process->mmap.text = img.text_vaddr;
  process->mmap.stack_bottom = stack_vaddr;
  process->mmap.stack_top = stack_vaddr + PAGE_SIZE - 4;
  process->mmap.data = img.data_vaddr;
  process->mmap.heap = img.data_vaddr + u_page_align_up(img.data_len);
  if (process->mmap.heap == 0)
    process->mmap.heap = img.text_vaddr + u_page_align_up(img.text_len);
  process->uregs.eip = img.entry;
  process->uregs.esp = process->mmap.stack_top;

  interrupt_restore(eflags);
  return 0;
}

// Add a process to the scheduler queue.
void process_schedule(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();
  if (process->list_node) {
    interrupt_restore(eflags);
    return;
  }
  list_push_front(&running_lists[process->priority], process);
  process->list_node = running_lists[process->priority].head;
  interrupt_restore(eflags);
}

// Remove a process from the scheduler queue.
void process_unschedule(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();
  if (process->list_node == NULL) {
    interrupt_restore(eflags);
    return;
  }
  list_remove(&running_lists[process->priority], process->list_node, 0);
  kfree(process->list_node);
  process->list_node = NULL;
  interrupt_restore(eflags);
}

// Kill a process.
void process_kill(process_t *process)
{
  if (process == NULL || process == init_process)
    return;
  if (process->has_ui)
    ui_kill(process);

  // Disable interrupts here to avoid contesting FD/PID locks.
  uint32_t eflags = interrupt_save_disable();

  // Close all FDs
  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i) {
    process_fd_t *fd = process->fds[i];
    if (fd == NULL)
      continue;
    fd->refcount--;
    if (fd->refcount == 0) {
      fs_close(&(fd->node));
      kfree(fd);
    }
  }

  // Wake all processes waiting for this one to die
  pids[process->pid].process = NULL;
  while (pids[process->pid].waiters.size) {
    list_node_t *head = pids[process->pid].waiters.head;
    uint32_t waiter_pid = (uint32_t)head->value;
    if (pids[waiter_pid].process) {
      pids[waiter_pid].process->uregs.eax = (process->exited & 1) |
                                            ((process->exit_status & 0x7fff) << 1) |
                                            ((process->next_signal & 0xFFFF) << 16);
      process_schedule(pids[waiter_pid].process);
    }
    list_remove(&pids[process->pid].waiters, head, 0);
    kfree(head);
  }

  // Re-parent child processes and kill child threads
  for (uint32_t i = 0; i < MAX_PROCESS_COUNT; ++i) {
    if (pids[i].process && pids[i].parent_pid == process->pid) {
      pids[i].parent_pid = 0;
      if (pids[i].process->is_thread)
        process_kill(pids[i].process);
    }
  }

  // Unmap and free userspace memory.
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(process->cr3);

  if (process->is_thread == 0) {
    uint8_t res = paging_clear_user_space();
    if (res) {
      log_error("process", "Failed to clear user address space.\n");
      paging_set_cr3(cr3);
      interrupt_restore(eflags);
      return;
    }
  } else {
    for (uint32_t va = process->mmap.stack_bottom; va < process->mmap.stack_top; va += PAGE_SIZE) {
      uint32_t pa = paging_get_paddr(va);
      if (pa)
        pmm_free(pa, 1);
      paging_unmap(va);
    }
  }

  paging_set_cr3(cr3);

  // Free kernel memory.
  kernel_stack_page_free(process->mmap.kernel_stack_bottom);

  if (!process->is_thread) {
    // Switch to init process memory space if we are about
    // to free the current page directory.
    if (process == current_process) {
      paging_copy_kernel_space(init_process->cr3);
      paging_set_cr3(init_process->cr3);
    }
    pmm_free(process->cr3, 1);
  }

  process_unschedule(process);
  kfree(process->wd);
  kfree(process);

  // If we just killed the current process, switch to the next process
  // instead of restoring interrupt state.
  if (process == current_process)
    process_switch_next();
  else
    interrupt_restore(eflags);
}
