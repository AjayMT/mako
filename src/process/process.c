
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
#include <klock/klock.h>
#include <fs/fs.h>
#include <pmm/pmm.h>
#include <paging/paging.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include <common/errno.h>
#include "process.h"

#define CHECK(err, msg, code) if ((err)) {          \
    log_error("process", msg "\n"); return (code);  \
  }
#define CHECK_UNLOCK_F(err, msg, code) if ((err)) {                 \
    log_error("process", msg "\n"); kunlock(&process_fork_lock);    \
    return (code);                                                  \
  }
#define CHECK_UNLOCK_L(err, msg, code) if ((err)) {                 \
    log_error("process", msg "\n"); kunlock(&process_load_lock);    \
    return (code);                                                  \
  }
#define CHECK_UNLOCK_S(err, msg, code) if ((err)) {                     \
    log_error("process", msg "\n"); kunlock(&process_schedule_lock);    \
    return (code);                                                      \
  }

// Constants.
static const uint32_t SCHEDULER_INTERVAL = 2;
static const uint32_t USER_MODE_CS       = 0x18;
static const uint32_t USER_MODE_DS       = 0x20;

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
static list_t *running_list = NULL;
static list_t *ready_queue = NULL;
static volatile uint32_t process_fork_lock = 0;
static volatile uint32_t process_load_lock = 0;
static volatile uint32_t process_schedule_lock = 0;

// Implemented in process.s.
void enter_kernelmode(process_registers_t *);

// Save the registers of the current process.
void update_current_process_registers(
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
    current_process->regs.esp = cstate.esp + sizeof(stack_state_t);
    current_process->regs.ss = SEGMENT_SELECTOR_KERNEL_CS;
  }
}

// Switch processes.
void process_switch(process_t *process)
{
  current_process = process;
  tss_set_kernel_stack(
    SEGMENT_SELECTOR_KERNEL_DS, process->mmap.kernel_stack_top
    );
  paging_set_cr3(process->cr3);
  if (process->regs.cs == (USER_MODE_CS | 3))
    enter_usermode(&(process->regs));
  else enter_kernelmode(&(process->regs));
}

// Switch to next scheduled process.
uint32_t process_switch_next()
{
  CHECK(current_process == NULL, "No current process", 1);

  list_node_t *insertion_node = current_process->list_node;
  while (ready_queue->size) {
    list_node_t *head = ready_queue->head;
    list_remove(ready_queue, head, 0);
    process_t *p = head->value;
    kfree(head);
    list_insert_after(running_list, insertion_node, p);
    p->list_node = insertion_node->next;
    insertion_node = insertion_node->next;
  }

  process_t *next = current_process;
  do {
    list_node_t *node = next->list_node->next;
    if (next->is_finished) {
      uint8_t res = process_destroy(next);
      CHECK(res, "Failed to destroy process.", res);
    }

    if (node == NULL) node = running_list->head;
    next = node->value;
  } while (next->is_running == 0 && next != current_process);

  uint32_t res = paging_copy_kernel_space(next->cr3);
  CHECK(res, "Failed to copy kernel address space.", res);

  process_switch(next);

  return 0;
}

// Interrupt handler that switches processes.
static void scheduler_interrupt_handler(
  cpu_state_t cstate, idt_info_t info, stack_state_t sstate
  )
{
  disable_interrupts();

  if (running_list->size == 0 && ready_queue->size == 0)
    return;

  if (current_process)
    update_current_process_registers(cstate, sstate);
  else if (ready_queue->size) {
    list_node_t *head = ready_queue->head;
    list_remove(ready_queue, head, 0);
    current_process = head->value;
    kfree(head);
    list_push_front(running_list, current_process);
    current_process->list_node = running_list->head;
  }

  process_switch_next();
}

// Initialize the scheduler and other things.
uint32_t process_init()
{
  process_tree = tree_init(NULL);
  running_list = kmalloc(sizeof(list_t));
  CHECK(running_list == NULL, "No memory.", ENOMEM);
  u_memset(running_list, 0, sizeof(list_t));
  ready_queue = kmalloc(sizeof(list_t));
  CHECK(ready_queue == NULL, "No memory.", ENOMEM);
  u_memset(ready_queue, 0, sizeof(list_t));

  pit_set_interval(SCHEDULER_INTERVAL);
  register_interrupt_handler(32, scheduler_interrupt_handler);

  return 0;
}

// Get current process.
process_t *process_current()
{ return current_process; }

// Create the `init` process.
uint32_t process_create_init(process_t *out_init, process_image_t img)
{
  uint32_t eflags = interrupt_save_disable();

  process_t *init = kmalloc(sizeof(process_t));
  CHECK(init == NULL, "No memory.", ENOMEM);
  u_memset(init, 0, sizeof(process_t));
  u_memcpy(init->name, "init", 5);
  init->wd = kmalloc(u_strlen("/") + 1);
  CHECK(init->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(init->wd, "/", u_strlen("/") + 1);
  init->is_running = 1;

  page_directory_t kernel_pd; uint32_t kernel_cr3;
  paging_get_kernel_pd(&kernel_pd, &kernel_cr3);
  uint32_t err = paging_clone_process_directory(&(init->cr3), kernel_cr3);
  CHECK(err, "Failed to clone page directory.", err);

  uint32_t kstack_vaddr = paging_prev_vaddr(1, PD_VADDR);
  CHECK(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1);
  CHECK(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK(res != PAGING_OK, "Failed to map kernel stack.", res);
  init->mmap.kernel_stack_bottom = kstack_vaddr;
  init->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 1;

  err = process_load(init, img);
  CHECK(err, "Failed to load process image.", err);
  process_tree->value = init;
  init->tree_node = process_tree;
  init->regs.cs = USER_MODE_CS | 3;
  init->regs.ss = USER_MODE_DS | 3;
  init->regs.eflags = 0x202;
  init_process = init;
  u_memcpy(out_init, init, sizeof(process_t));

  interrupt_restore(eflags);
  return 0;
}

// Fork a process.
uint32_t process_fork(process_t *out_child, process_t *process)
{
  klock(&process_fork_lock);

  process_t *child = kmalloc(sizeof(process_t));
  CHECK_UNLOCK_F(child == NULL, "No memory.", ENOMEM);
  u_memcpy(child, process, sizeof(process_t));
  child->wd = kmalloc(u_strlen(process->wd) + 1);
  CHECK_UNLOCK_F(child->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(child->wd, process->wd, u_strlen(process->wd) + 1);
  child->pid = ++next_pid;
  tree_node_t *node = tree_init(child);
  child->tree_node = node;
  tree_insert(process->tree_node, node);
  child->list_node = NULL;
  uint32_t err = paging_clone_process_directory(&(child->cr3), process->cr3);
  CHECK_UNLOCK_F(err, "Failed to clone page directory.", err);

  fs_node_t *nodes = kmalloc(sizeof(fs_node_t) * process->fds.capacity);
  CHECK_UNLOCK_F(process->fds.capacity && nodes == NULL, "No memory.", ENOMEM);
  for (uint32_t i = 0; i < process->fds.size; ++i)
    u_memcpy(nodes + i, process->fds.nodes + i, sizeof(fs_node_t));
  child->fds.nodes = nodes;

  uint32_t kstack_vaddr = paging_prev_vaddr(1, PD_VADDR);
  CHECK_UNLOCK_F(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1);
  CHECK_UNLOCK_F(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK_UNLOCK_F(res != PAGING_OK, "Failed to map kernel stack.", res);
  child->mmap.kernel_stack_bottom = kstack_vaddr;
  child->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 1;
  u_memcpy(out_child, child, sizeof(process_t));

  kunlock(&process_fork_lock);
  return 0;
}

// Overwrite a process image.
uint32_t process_load(process_t *process, process_image_t img)
{
  CHECK(img.text_len == 0, "No text.", 1);

  klock(&process_load_lock);
  uint32_t cr3 = paging_get_cr3();

  paging_set_cr3(process->cr3);
  uint32_t err = paging_clear_user_space();
  CHECK_UNLOCK_L(err, "Failed to clear user address space.", err);

  uint32_t npages = page_align_up(img.text_len) >> PHYS_ADDR_OFFSET;
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.user = 1;
  flags.rw = 1;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_UNLOCK_L(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res = paging_map(
      (uint32_t)img.text_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags
      );
    CHECK_UNLOCK_L(res != PAGING_OK, "Failed to map text pages.", res);
  }

  u_memcpy((uint8_t *)img.text_vaddr, img.text, img.text_len);

  npages = page_align_up(img.data_len) >> PHYS_ADDR_OFFSET;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_UNLOCK_L(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res = paging_map(
      (uint32_t)img.data_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags
      );
    CHECK_UNLOCK_L(res != PAGING_OK, "Failed to map data pages.", res);
  }

  u_memcpy((uint8_t *)img.data_vaddr, img.data, img.data_len);

  uint32_t stack_paddr = pmm_alloc(1);
  CHECK_UNLOCK_L(stack_paddr == 0, "No memory.", ENOMEM);
  uint32_t stack_vaddr = paging_prev_vaddr(1, KERNEL_START_VADDR);
  CHECK_UNLOCK_L(stack_vaddr == 0, "No memory.", ENOMEM);
  paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
  CHECK_UNLOCK_L(res != PAGING_OK, "Failed to map stack pages.", res);

  paging_set_cr3(cr3);

  process->mmap.text = img.text_vaddr;
  process->mmap.stack_bottom = stack_vaddr;
  process->mmap.stack_top = stack_vaddr + PAGE_SIZE - 1;
  process->mmap.data = img.data_vaddr;
  process->mmap.heap = img.data_vaddr + page_align_up(img.data_len);
  process->brk = process->mmap.heap;
  process->regs.eip = img.entry;
  process->regs.ebp = process->mmap.stack_top;
  process->regs.esp = process->mmap.stack_top;

  kunlock(&process_load_lock);
  return 0;
}

// Add a process to the scheduler queue.
void process_schedule(process_t *process)
{
  klock(&process_schedule_lock);
  list_push_back(ready_queue, process);
  kunlock(&process_schedule_lock);
}

// Mark a process as finished, deal with children.
void process_finish(process_t *process)
{
  klock(&process_schedule_lock);

  process->is_finished = 1;
  tree_node_t *node = process->tree_node;
  list_foreach(lchild, node->children) {
    tree_node_t *tchild = lchild->value;
    process_t *child_process = tchild->value;

    // Threads just die, child processes become children of init.
    if (child_process->is_thread) process_finish(child_process);
    else {
      list_node_t *next = lchild->next;
      list_remove(node->children, lchild, 0);
      tree_insert(process_tree, tchild);
      kfree(lchild);
      lchild = next;
    }
  }

  kunlock(&process_schedule_lock);
}

// Destroy a process.
uint8_t process_destroy(process_t *process)
{
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(process->cr3);
  uint8_t res = paging_clear_user_space();
  CHECK(res, "Failed to clear user address space.", res);
  paging_set_cr3(cr3);
  pmm_free(process->cr3, 1);

  for (
    uint32_t vaddr = process->mmap.kernel_stack_bottom;
    vaddr < process->mmap.kernel_stack_top;
    vaddr += PAGE_SIZE
    )
  {
    pmm_free(paging_get_paddr(vaddr), 1);
    paging_result_t res = paging_unmap(vaddr);
    CHECK(res != PAGING_OK, "paging_unmap failed.", res);
  }

  tree_node_t *tree_node = process->tree_node;
  list_node_t *list_node = process->list_node;
  list_remove(running_list, list_node, 0);
  list_foreach(lchild, tree_node->parent->children) {
    if (lchild->value == tree_node) {
      list_remove(tree_node->parent->children, lchild, 0);
      break;
    }
  }

  kfree(process->wd);
  kfree(process->fds.nodes);
  kfree(tree_node);
  kfree(list_node);
  kfree(process);

  return 0;
}
