
// process.c
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include "../common/stdint.h"
#include "interrupt.h"
#include "rtc.h"
#include "pit.h"
#include "tss.h"
#include "ds.h"
#include "kheap.h"
#include "klock.h"
#include "fs.h"
#include "pipe.h"
#include "pmm.h"
#include "paging.h"
#include "fpu.h"
#include "ui.h"
#include "util.h"
#include "log.h"
#include "constants.h"
#include "../common/signal.h"
#include "../common/errno.h"
#include "process.h"

#define CHECK(err, msg, code) if ((err)) {              \
    log_error("process", msg "\n"); return (code);      \
  }
#define CHECK_UNLOCK(err, msg, code) if ((err)) {                       \
    log_error("process", msg "\n"); kunlock(&process_tree_lock);        \
    return (code);                                                      \
  }

// Constants.
static const uint32_t USER_MODE_CS = 0x18;
static const uint32_t USER_MODE_DS = 0x20;
static const uint32_t ENV_VADDR    = KERNEL_START_VADDR - PAGE_SIZE;

static uint32_t next_pid = 0;
static process_t *init_process = NULL;
static process_t *destroyer_process = NULL;
static process_t *current_process = NULL;
static list_t *sleep_queue = NULL;
static list_t *destroy_queue = NULL;
static volatile uint32_t destroy_queue_lock = 0;

static list_t *running_lists[MAX_PROCESS_PRIORITY + 1];

// Implemented in process.s.
void resume_kernel(process_registers_t *);

// Save the registers of the current process.
void update_current_process_registers(
  cpu_state_t cstate, stack_state_t sstate
  )
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
  if (current_process) fpu_save(current_process);
  fpu_restore(process);
  current_process = process;
  tss_set_kernel_stack(
    SEGMENT_SELECTOR_KERNEL_DS, process->mmap.kernel_stack_top
    );
  paging_set_cr3(process->cr3);
  if (process->in_kernel) resume_kernel(&(process->kregs));
  else enter_usermode(&(process->uregs));
}

// Switch to next scheduled process.
uint32_t process_switch_next()
{
  uint32_t eflags = interrupt_save_disable();
  list_t *running_list;
  for (int32_t i = MAX_PROCESS_PRIORITY; i >= 0; --i) {
    running_list = running_lists[i];
    if (running_list->size) break;
  }
  if (running_list->size == 0) {
    interrupt_restore(eflags);
    log_error("process", "Empty running list.");
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
  if (running_list->tail) running_list->tail->next = next->list_node;
  running_list->tail = next->list_node;
  if (running_list->head == NULL) running_list->head = next->list_node;
  running_list->size++;

  if (next->in_kernel) {
    process_resume(next);
    return 0;
  }

  // Jump to signal handler or UI event handler if necessary
  if (next->next_signal && next->current_signal == 0) {
    u_memcpy(&(next->saved_signal_regs), &(next->uregs), sizeof(process_registers_t));
    next->current_signal = next->next_signal;
    next->next_signal = 0;
    next->uregs.eip = next->signal_eip;
    next->uregs.edi = next->current_signal;
  } else if (
    next->ui_event_queue->size
    && next->ui_state != PR_UI_EVENT
    && next->ui_eip
    && next->ui_event_buffer
    ) {
    ui_event_t *next_event = next->ui_event_queue->head->value;
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(next->cr3);
    u_memcpy((ui_event_t *)next->ui_event_buffer, next_event, sizeof(ui_event_t));
    paging_set_cr3(cr3);
    u_memcpy(
      &(next->saved_ui_regs), &(next->uregs), sizeof(process_registers_t)
      );
    next->uregs.eip = next->ui_eip;
    next->ui_state = PR_UI_EVENT;
  }

  process_resume(next);

  return 0;
}

// General protection fault handler.
static void gp_fault_handler(
  cpu_state_t cs, idt_info_t info, stack_state_t ss
  )
{
  log_error(
    "process", "eip %x: gpf %x cs %x\n",
    ss.eip, info.error_code, ss.cs
    );
  process_kill(current_process);
  current_process->current_signal = SIGILL;
  process_switch_next();
}

// Page fault handler.
static void page_fault_handler(
  cpu_state_t cs, idt_info_t info, stack_state_t ss
  )
{
  uint32_t vaddr;
  asm("movl %%cr2, %0" : "=r"(vaddr));
  log_error(
    "process", "eip %x: page fault %x vaddr %x esp %x pid %u\n",
    ss.eip, info.error_code, vaddr, ss.user_esp, current_process ? current_process->pid : 0
    );
  while (current_process == NULL);
  if (ss.cs == (USER_MODE_CS | 3)) {
    uint32_t stb = current_process->mmap.stack_bottom;
    if (vaddr < stb && stb - vaddr < PAGE_SIZE) {
      uint32_t paddr = pmm_alloc(1);
      if (paddr == 0) goto die;
      uint32_t vaddr = stb - PAGE_SIZE;
      page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
      flags.rw = 1; flags.user = 1;
      paging_result_t res = paging_map(vaddr, paddr, flags);
      if (res != PAGING_OK) goto die;
      current_process->mmap.stack_bottom = vaddr;
      log_info(
        "process", "grew the stack to %x for process %u\n",
        vaddr, current_process->pid
        );
      return;
    }

    process_signal(current_process, SIGSEGV);
    return;
  }

  // Yikes, kernel page fault.
die:
  process_kill(current_process);
  current_process->current_signal = SIGSEGV;
  process_switch_next();
}

// Interrupt handler that switches processes.
static void scheduler_interrupt_handler(
  cpu_state_t cstate, idt_info_t info, stack_state_t sstate
  )
{
  uint32_t eflags = interrupt_save_disable();

  uint32_t running_count = 0;
  for (uint32_t i = 0; i <= MAX_PROCESS_PRIORITY; ++i) running_count += running_lists[i]->size;
  if (running_count == 0) { interrupt_restore(eflags); return; }

  if (current_process) update_current_process_registers(cstate, sstate);

  while (sleep_queue->size) {
    process_sleep_node_t *sleeper = sleep_queue->head->value;
    uint32_t current_time = pit_get_time();
    // Each RTC tick is approximately 1 ms.
    if ((int32_t)(sleeper->wake_time - current_time) <= 1) {
      process_schedule(sleeper->process);
      list_pop_front(sleep_queue);
    } else break;
  }

  process_switch_next();
  interrupt_restore(eflags);
}

// Create the destroyer process.
static uint32_t process_create_destroyer();

// Initialize the scheduler and other things.
uint32_t process_init()
{
  for (uint32_t i = 0; i <= MAX_PROCESS_PRIORITY; ++i) {
    running_lists[i] = kmalloc(sizeof(list_t));
    CHECK(running_lists[i] == NULL, "No memory.", ENOMEM);
    u_memset(running_lists[i], 0, sizeof(list_t));
  }

  sleep_queue = kmalloc(sizeof(list_t));
  CHECK(sleep_queue == NULL, "No memory.", ENOMEM);
  u_memset(sleep_queue, 0, sizeof(list_t));
  destroy_queue = kmalloc(sizeof(list_t));
  CHECK(destroy_queue == NULL, "No memory.", ENOMEM);
  u_memset(destroy_queue, 0, sizeof(list_t));

  uint32_t err = process_create_destroyer();
  CHECK(err, "Failed to create destroyer.", err);

  rtc_set_handler(scheduler_interrupt_handler);
  register_interrupt_handler(13, gp_fault_handler);
  register_interrupt_handler(14, page_fault_handler);

  return 0;
}

static process_t *findpid(tree_node_t *node, uint32_t pid)
{
  if (node == NULL) return NULL;

  process_t *p = node->value;
  if (p->pid == pid) return p;
  list_foreach(lchild, node->children) {
    tree_node_t *tchild = lchild->value;
    klock(&(p->tree_lock));
    process_t *res = findpid(tchild, pid);
    kunlock(&(p->tree_lock));
    if (res) return res;
  }

  return NULL;
}

// Find a process with a specific PID.
process_t *process_from_pid(uint32_t pid)
{
  if (init_process == NULL) return NULL;
  klock(&(init_process->tree_lock));
  process_t *p = findpid(init_process->tree_node, pid);
  kunlock(&(init_process->tree_lock));
  return p;
}

// Add a process to the sleep queue.
uint32_t process_sleep(process_t *p, uint32_t wake_time)
{

  process_sleep_node_t *sleeper = kmalloc(sizeof(process_sleep_node_t));
  CHECK(sleeper == NULL, "Failed to allocate sleep node.", ENOMEM);
  sleeper->process = p;
  sleeper->wake_time = wake_time;

  // This should really be a min heap.
  list_node_t *position = sleep_queue->head;
  list_foreach(lchild, sleep_queue) {
    process_sleep_node_t *node = lchild->value;
    if (node->wake_time < sleeper->wake_time)
      position = lchild;
    else break;
  }
  if (position == NULL) list_push_back(sleep_queue, sleeper);
  else list_insert_before(sleep_queue, position, sleeper);

  return 0;
}

// Send a signal to a process.
void process_signal(process_t *p, uint32_t signum)
{
  if (signum == 0) return;
  p->next_signal = signum;
  if (p->signal_eip == 0) process_kill(p);
  if (p == current_process) process_switch_next();
}

// Get current process.
process_t *process_current()
{ return current_process; }

// Create the `init` process.
uint32_t process_create_schedule_init(process_image_t img)
{
  process_t *init = kmalloc(sizeof(process_t)); CHECK(init == NULL, "No memory.", ENOMEM);
  u_memset(init, 0, sizeof(process_t));
  init->wd = kmalloc(2); CHECK(init->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(init->wd, "/", u_strlen("/") + 1);
  init->ui_event_queue = kmalloc(sizeof(list_t)); CHECK(init->ui_event_queue == NULL, "No memory.", ENOMEM);
  u_memset(init->ui_event_queue, 0, sizeof(list_t));

  page_directory_t kernel_pd; uint32_t kernel_cr3;
  paging_get_kernel_pd(&kernel_pd, &kernel_cr3);
  uint32_t err = paging_clone_process_directory(&(init->cr3), kernel_cr3);
  CHECK(err, "Failed to clone page directory.", err);

  uint32_t kstack_vaddr = paging_prev_vaddr(1, FIRST_PT_VADDR);
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
  init->tree_node = tree_init(init);
  init->uregs.cs = USER_MODE_CS | 3;
  init->uregs.ss = USER_MODE_DS | 3;
  init->uregs.eflags = 0x202;

  init_process = init;
  process_schedule(init);

  return 0;
}

#define CHECK_RESTORE(err, msg, code) if ((err)) {              \
    log_error("process", msg "\n"); paging_set_cr3(cr3);        \
    interrupt_restore(eflags); return (code);                   \
  }

// Fork a process.
uint32_t process_fork(
  process_t *child, process_t *process, uint8_t is_thread
  )
{
  u_memcpy(child, process, sizeof(process_t));
  kunlock(&child->fd_lock);
  kunlock(&child->tree_lock);
  child->in_kernel = 0;
  child->is_thread = is_thread;
  child->wd = kmalloc(u_strlen(process->wd) + 1); CHECK(child->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(child->wd, process->wd, u_strlen(process->wd) + 1);
  child->pid = ++next_pid;
  child->gid = child->pid;
  tree_node_t *node = tree_init(child);
  child->tree_node = node;
  klock(&process->tree_lock); tree_insert(process->tree_node, node); kunlock(&process->tree_lock);
  child->list_node = NULL;
  child->ui_event_queue = kmalloc(sizeof(list_t)); CHECK(child->ui_event_queue == NULL, "No memory.", ENOMEM);
  u_memset(child->ui_event_queue, 0, sizeof(list_t));
  child->ui_eip = 0; child->ui_event_buffer = 0; child->ui_state = PR_UI_NONE;

  if (is_thread) {
    child->gid = process->gid;
    uint32_t eflags = interrupt_save_disable();
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(process->cr3);

    uint32_t stack_vaddr = paging_prev_vaddr(1, process->mmap.text);
    CHECK_RESTORE(stack_vaddr == 0, "Failed to allocate thread stack virtual page.", ENOMEM);
    uint32_t stack_paddr = pmm_alloc(1);
    CHECK_RESTORE(stack_paddr == 0, "Failed to allocate thread stack physical page.", ENOMEM);

    page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
    flags.rw = 1; flags.user = 1;
    paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
    CHECK_RESTORE(res != PAGING_OK, "Failed to map thread stack page.", res);

    child->mmap.stack_top = stack_vaddr + PAGE_SIZE - 1;
    child->mmap.stack_bottom = stack_vaddr;

    paging_set_cr3(cr3);
    interrupt_restore(eflags);
  } else {
    uint32_t err = paging_clone_process_directory(&(child->cr3), process->cr3);
    CHECK(err, "Failed to clone page directory.", err);
  }

  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i)
    if (child->fds[i]) child->fds[i]->refcount++;

  uint32_t eflags = interrupt_save_disable();
  uint32_t kstack_vaddr = paging_prev_vaddr(1, FIRST_PT_VADDR);
  CHECK(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1);
  CHECK(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK(res != PAGING_OK, "Failed to map kernel stack.", res);
  child->mmap.kernel_stack_bottom = kstack_vaddr;
  child->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 1;
  interrupt_restore(eflags);

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
  CHECK_RESTORE(err, "Failed to clear user address space.", err);

  uint32_t npages = u_page_align_up(img.text_len) >> PHYS_ADDR_OFFSET;
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.user = 1;
  flags.rw = 1;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_RESTORE(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res = paging_map(
      (uint32_t)img.text_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags
      );
    CHECK_RESTORE(res != PAGING_OK, "Failed to map text pages.", res);
  }

  u_memcpy((uint8_t *)img.text_vaddr, img.text, img.text_len);

  npages = u_page_align_up(img.data_len) >> PHYS_ADDR_OFFSET;
  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    CHECK_RESTORE(paddr == 0, "No memory.", ENOMEM);
    paging_result_t res = paging_map(
      (uint32_t)img.data_vaddr + (i << PHYS_ADDR_OFFSET), paddr, flags
      );
    CHECK_RESTORE(res != PAGING_OK, "Failed to map data pages.", res);
  }

  u_memcpy((uint8_t *)img.data_vaddr, img.data, img.data_len);

  uint32_t stack_paddr = pmm_alloc(1);
  CHECK_RESTORE(stack_paddr == 0, "No memory.", ENOMEM);
  // Leave one page between the stack and the kernel for environment variables.
  uint32_t stack_vaddr = paging_prev_vaddr(1, KERNEL_START_VADDR - PAGE_SIZE);
  CHECK_RESTORE(stack_vaddr == 0, "No memory.", ENOMEM);
  paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
  CHECK_RESTORE(res != PAGING_OK, "Failed to map stack pages.", res);

  uint32_t env_paddr = pmm_alloc(1);
  CHECK_RESTORE(env_paddr == 0, "No memory.", ENOMEM);
  flags.user = 1;
  flags.rw = 1;
  res = paging_map(ENV_VADDR, env_paddr, flags);
  CHECK_RESTORE(res != PAGING_OK, "Failed to map env page.", res);

  paging_set_cr3(cr3);

  process->mmap.text = img.text_vaddr;
  process->mmap.stack_bottom = stack_vaddr;
  process->mmap.stack_top = stack_vaddr + PAGE_SIZE - 1;
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
  if (process->list_node) { interrupt_restore(eflags); return; }
  list_push_front(running_lists[process->priority], process);
  process->list_node = running_lists[process->priority]->head;
  interrupt_restore(eflags);
}

// Remove a process from the scheduler queue.
void process_unschedule(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();
  if (process->list_node == NULL) { interrupt_restore(eflags); return; }
  list_remove(running_lists[process->priority], process->list_node, 0);
  kfree(process->list_node);
  process->list_node = NULL;
  interrupt_restore(eflags);
}

// Kill a process.
void process_kill(process_t *process)
{
  if (process == init_process) return;
  if (process->ui_eip) ui_kill(process);

  klock(&process->tree_lock);
  tree_node_t *tree_node = process->tree_node;
  list_node_t *current = tree_node->children->head;
  list_node_t *next = current ? current->next : NULL;
  for (; current; current = next) {
    next = current->next;
    tree_node_t *tchild = current->value;
    process_t *child_process = tchild->value;

    // Threads just die, child processes become children of init.
    if (child_process->is_thread)
      process_kill(child_process);
    else {
      list_remove(tree_node->children, current, 0);
      klock(&init_process->tree_lock);
      tree_insert(init_process->tree_node, tchild);
      kunlock(&init_process->tree_lock);
      kfree(current);
    }
  }
  kunlock(&process->tree_lock);

  if (tree_node->parent) {
    process_t *parent_process = tree_node->parent->value;
    klock(&parent_process->tree_lock);
    list_foreach(lchild, tree_node->parent->children) {
      if (lchild->value == tree_node) {
        list_remove(tree_node->parent->children, lchild, 0);
        kfree(lchild);
        break;
      }
    }
    kunlock(&parent_process->tree_lock);
  }

  kfree(tree_node); process->tree_node = NULL;

  // TODO wake all waiters

  klock(&destroy_queue_lock);
  uint32_t eflags = interrupt_save_disable();

  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i) {
    process_fd_t *fd = process->fds[i];
    if (fd == NULL) continue;
    fd->refcount--;
    if (fd->refcount == 0) {
      fs_close(&(fd->node));
      kfree(fd);
    }
  }

  process_unschedule(process);
  list_push_back(destroy_queue, process);
  process_schedule(destroyer_process);
  interrupt_restore(eflags);
  kunlock(&destroy_queue_lock);
}

// Destroys dead processes.
static void process_destroyer()
{
  klock(&destroy_queue_lock);
  while (destroy_queue->size) {
    list_node_t *head = destroy_queue->head;
    list_remove(destroy_queue, head, 0);
    kunlock(&destroy_queue_lock);

    process_t *process = head->value;
    kfree(head);

    uint32_t eflags = interrupt_save_disable();

    if (process->is_thread == 0) {
      uint32_t cr3 = paging_get_cr3();
      uint32_t eflags = interrupt_save_disable();
      paging_set_cr3(process->cr3);
      uint8_t res = paging_clear_user_space();
      if (res) {
        log_error("process", "Failed to clear user address space.\n");
        paging_set_cr3(cr3);
        interrupt_restore(eflags);
        break;
      }
      paging_set_cr3(cr3);
      pmm_free(process->cr3, 1);
    } else {
      uint32_t cr3 = paging_get_cr3();
      paging_set_cr3(process->cr3);
      for (
        uint32_t vaddr = process->mmap.stack_bottom;
        vaddr < process->mmap.stack_top;
        vaddr += PAGE_SIZE
        )
      {
        uint32_t paddr = paging_get_paddr(vaddr);
        if (paddr) pmm_free(paddr, 1);
        paging_unmap(vaddr);
      }
      paging_set_cr3(cr3);
    }

    for (
      uint32_t vaddr = process->mmap.kernel_stack_bottom;
      vaddr < process->mmap.kernel_stack_top;
      vaddr += PAGE_SIZE
      )
    {
      pmm_free(paging_get_paddr(vaddr), 1);
      paging_result_t res = paging_unmap(vaddr);
      if (res != PAGING_OK) {
        log_error("process", "paging_unmap failed.");
        continue;
      }
    }

    interrupt_restore(eflags);

    list_destroy(process->ui_event_queue);
    kfree(process->wd);
    kfree(process);

    klock(&destroy_queue_lock);
  }

  kunlock(&destroy_queue_lock);

  disable_interrupts();
  current_process->kregs.eip = (uint32_t)process_destroyer;
  current_process->kregs.esp = current_process->mmap.kernel_stack_top;
  current_process->kregs.cs = SEGMENT_SELECTOR_KERNEL_CS;
  current_process->kregs.ss = SEGMENT_SELECTOR_KERNEL_DS;
  process_unschedule(current_process);
  process_switch_next();
}

// Create the destroyer process.
static uint32_t process_create_destroyer()
{
  process_t *d = kmalloc(sizeof(process_t)); CHECK(d == NULL, "No memory.", ENOMEM);
  u_memset(d, 0, sizeof(process_t));
  d->ui_event_queue = kmalloc(sizeof(list_t)); CHECK(d->ui_event_queue == NULL, "No memory.", ENOMEM);
  u_memset(d->ui_event_queue, 0, sizeof(list_t));

  page_directory_t kernel_pd; uint32_t kernel_cr3;
  paging_get_kernel_pd(&kernel_pd, &kernel_cr3);
  d->cr3 = kernel_cr3;

  uint32_t kstack_vaddr = paging_prev_vaddr(1, FIRST_PT_VADDR); CHECK(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1); CHECK(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK(res != PAGING_OK, "Failed to map kernel stack.", res);
  d->mmap.kernel_stack_bottom = kstack_vaddr;
  d->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 1;

  d->in_kernel = 1;
  d->kregs.esp = d->mmap.kernel_stack_top;
  d->kregs.eip = (uint32_t)process_destroyer;
  d->kregs.cs = SEGMENT_SELECTOR_KERNEL_CS;
  d->kregs.ss = SEGMENT_SELECTOR_KERNEL_DS;

  destroyer_process = d;

  return 0;
}
