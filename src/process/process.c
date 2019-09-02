
// process.c
//
// Process management and user mode.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <stddef.h>
#include <stdint.h>
#include <interrupt/interrupt.h>
#include <rtc/rtc.h>
#include <pit/pit.h>
#include <tss/tss.h>
#include <ds/ds.h>
#include <kheap/kheap.h>
#include <klock/klock.h>
#include <fs/fs.h>
#include <pipe/pipe.h>
#include <pmm/pmm.h>
#include <paging/paging.h>
#include <fpu/fpu.h>
#include <ui/ui.h>
#include <util/util.h>
#include <debug/log.h>
#include <common/constants.h>
#include <common/signal.h>
#include <common/errno.h>
#include "process.h"

#define CHECK(err, msg, code) if ((err)) {          \
    log_error("process", msg "\n"); return (code);  \
  }
#define CHECK_UNLOCK(err, msg, code) if ((err)) {                   \
    log_error("process", msg "\n"); kunlock(&process_tree_lock);    \
    return (code);                                                  \
  }

// Constants.
static const uint32_t USER_MODE_CS = 0x18;
static const uint32_t USER_MODE_DS = 0x20;
static const uint32_t ENV_VADDR    = KERNEL_START_VADDR - PAGE_SIZE;

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
static list_t *sleep_queue = NULL;
static volatile uint32_t process_tree_lock = 0;
static volatile uint32_t sleep_queue_lock = 0;

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

// Switch processes.
void process_switch(process_t *process)
{
  fpu_save(current_process);
  fpu_restore(process);
  current_process = process;
  tss_set_kernel_stack(
    SEGMENT_SELECTOR_KERNEL_DS, process->mmap.kernel_stack_top
    );
  paging_set_cr3(process->cr3);
  if (process->in_kernel) resume_kernel(&(process->kregs));
  else enter_usermode(&(process->uregs));
}

// Destroy a process.
static uint32_t process_destroy(process_t *);

// Switch to next scheduled process.
uint32_t process_switch_next()
{
  disable_interrupts();
  CHECK(current_process == NULL, "No current process", 1);

  list_node_t *insertion_node = current_process->list_node;
  while (ready_queue->size) {
    list_node_t *head = ready_queue->head;
    list_remove(ready_queue, head, 0);
    process_t *p = head->value;
    kfree(head);

    // Pretty sure this will never happen but
    // anything's possible with bad code :)
    if (p == current_process) continue;

    if (p->list_node) {
      list_remove(running_list, p->list_node, 0);
      kfree(p->list_node);
    }
    list_insert_after(running_list, insertion_node, p);
    p->list_node = insertion_node->next;
    insertion_node = insertion_node->next;
  }

  list_node_t *lnode = current_process->list_node->next;
  if (lnode == NULL) lnode = running_list->head;
  while (lnode->value != current_process) {
    list_node_t *next = lnode->next;
    if (next == NULL) next = running_list->head;
    process_t *p = lnode->value;
    if (p->is_finished || p->next_signal == SIGKILL) {
      uint32_t res = process_destroy(p);
      CHECK(res, "Failed to destroy process.", res);
    } else if (p->is_running) break;
    lnode = next;
  }

  process_t *next = lnode->value;
  uint32_t res = paging_copy_kernel_space(next->cr3);
  CHECK(res, "Failed to copy kernel address space.", res);

  if (
    next->next_signal && next->in_kernel == 0 && next->signal_pending == 0
    )
  {
    u_memcpy(
      &(next->saved_signal_regs), &(next->uregs), sizeof(process_registers_t)
      );
    next->signal_pending = next->next_signal;
    next->next_signal = 0;
    next->uregs.eip = next->signal_eip;
    next->uregs.ebx = next->signal_pending;
  } else if (
    next->ui_event_queue->size
    && next->ui_event_pending == 0
    && next->in_kernel == 0
    && next->ui_eip
    && next->ui_event_buffer
    )
  {
    ui_event_t *next_event = next->ui_event_queue->head->value;
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(next->cr3);
    u_memcpy(
      (ui_event_t *)next->ui_event_buffer, next_event, sizeof(ui_event_t)
      );
    paging_set_cr3(cr3);
    u_memcpy(
      &(next->saved_ui_regs), &(next->uregs), sizeof(process_registers_t)
      );
    next->uregs.eip = next->ui_eip;
    next->ui_event_pending = 1;
  }

  process_switch(next);

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
  process_finish(current_process);
  current_process->exited = 0;
  current_process->signal_pending = SIGILL;
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
    ss.eip, info.error_code, vaddr, ss.user_esp, current_process->pid
    );

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
        "process", "grew the stack to %x for process %u",
        vaddr, current_process->pid
        );
      return;
    }

    process_signal(current_process, SIGSEGV);
    return;
  }

  // Yikes, kernel page fault.
die:
  process_finish(current_process);
  current_process->exited = 0;
  current_process->signal_pending = SIGSEGV;
  process_switch_next();
}

// Interrupt handler that switches processes.
static void scheduler_interrupt_handler(
  cpu_state_t cstate, idt_info_t info, stack_state_t sstate
  )
{
  uint32_t eflags = interrupt_save_disable();

  if (running_list->size == 0 && ready_queue->size == 0) {
    interrupt_restore(eflags); return;
  }

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

  while (sleep_queue->size) {
    process_sleep_node_t *sleeper = sleep_queue->head->value;
    uint32_t current_time = pit_get_time();
    // Each RTC tick is approximately 7.8125 ms, but we're using ints.
    if ((int32_t)(sleeper->wake_time - current_time) <= 8) {
      sleeper->process->is_running = 1;
      process_schedule(sleeper->process);
      list_remove(sleep_queue, sleep_queue->head, 1);
    } else break;
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
  sleep_queue = kmalloc(sizeof(list_t));
  CHECK(sleep_queue == NULL, "No memory.", ENOMEM);
  u_memset(sleep_queue, 0, sizeof(list_t));

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
    process_t *res = findpid(tchild, pid);
    if (res) return res;
  }

  return NULL;
}

// Find a process with a specific PID.
process_t *process_from_pid(uint32_t pid)
{
  klock(&process_tree_lock);
  process_t *p = findpid(process_tree, pid);
  kunlock(&process_tree_lock);
  return p;
}

// Add a process to the sleep queue.
uint32_t process_sleep(process_t *p, uint32_t wake_time)
{
  klock(&sleep_queue_lock);

  process_sleep_node_t *sleeper = kmalloc(sizeof(process_sleep_node_t));
  if (sleeper == NULL) {
    kunlock(&sleep_queue_lock); return ENOMEM;
  }
  sleeper->process = p;
  sleeper->wake_time = wake_time;

  list_node_t *position = sleep_queue->head;
  list_foreach(lchild, sleep_queue) {
    process_sleep_node_t *node = lchild->value;
    if (node->wake_time < sleeper->wake_time)
      position = lchild;
    else break;
  }
  if (position == NULL) list_push_back(sleep_queue, sleeper);
  else list_insert_before(sleep_queue, position, sleeper);

  kunlock(&sleep_queue_lock);
  return 0;
}

// Send a signal to a process.
void process_signal(process_t *p, uint32_t signum)
{
  if (signum == 0) return;
  p->next_signal = signum;
  if (p->signal_eip == 0) process_finish(p);
  if (p == current_process) process_switch_next();
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
  init->fds = kmalloc(sizeof(list_t));
  CHECK(init->fds == NULL, "No memory.", ENOMEM);
  u_memset(init->fds, 0, sizeof(list_t));
  init->ui_event_queue = kmalloc(sizeof(list_t));
  CHECK(init->ui_event_queue == NULL, "No memory.", ENOMEM);
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
  process_tree->value = init;
  init->tree_node = process_tree;
  init->uregs.cs = USER_MODE_CS | 3;
  init->uregs.ss = USER_MODE_DS | 3;
  init->uregs.eflags = 0x202;
  init_process = init;
  u_memcpy(out_init, init, sizeof(process_t));

  interrupt_restore(eflags);
  return 0;
}

// Fork a process.
uint32_t process_fork(
  process_t *out_child, process_t *process, uint8_t is_thread
  )
{
  klock(&process_tree_lock);

  process_t *child = out_child;
  u_memcpy(child, process, sizeof(process_t));
  child->in_kernel = 0;
  child->is_thread = is_thread;
  child->wd = kmalloc(u_strlen(process->wd) + 1);
  CHECK_UNLOCK(child->wd == NULL, "No memory.", ENOMEM);
  u_memcpy(child->wd, process->wd, u_strlen(process->wd) + 1);
  child->pid = ++next_pid;
  child->gid = child->pid;
  tree_node_t *node = tree_init(child);
  child->tree_node = node;
  tree_insert(process->tree_node, node);
  child->list_node = NULL;
  child->ui_event_queue = kmalloc(sizeof(list_t));
  CHECK_UNLOCK(child->ui_event_queue == NULL, "No memory.", ENOMEM);
  u_memset(child->ui_event_queue, 0, sizeof(list_t));
  child->ui_eip = 0; child->ui_event_buffer = 0; child->ui_event_pending = 0;
  if (is_thread == 0) {
    uint32_t err = paging_clone_process_directory(&(child->cr3), process->cr3);
    CHECK_UNLOCK(err, "Failed to clone page directory.", err);
  } else {
    child->gid = process->gid;
    uint32_t eflags = interrupt_save_disable();
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(process->cr3);

    uint32_t stack_vaddr = paging_prev_vaddr(1, process->mmap.text);
    uint32_t err = ENOMEM;
    if (stack_vaddr == 0) {
    fail:
      paging_set_cr3(cr3);
      interrupt_restore(eflags);
      kunlock(&process_tree_lock);
      return err;
    }
    uint32_t stack_paddr = pmm_alloc(1);
    if (stack_paddr == 0) { err = ENOMEM; goto fail; }

    page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
    flags.rw = 1; flags.user = 1;
    paging_result_t res = paging_map(stack_vaddr, stack_paddr, flags);
    if (res != PAGING_OK) { err = res; goto fail; }

    child->mmap.stack_top = stack_vaddr + PAGE_SIZE - 1;
    child->mmap.stack_bottom = stack_vaddr;
    child->uregs.ebp = child->mmap.stack_top;
    child->uregs.esp = child->uregs.ebp;

    paging_set_cr3(cr3);
    interrupt_restore(eflags);
  }

  list_t *fds = kmalloc(sizeof(list_t));
  CHECK_UNLOCK(fds == NULL, "No memory.", ENOMEM);
  u_memset(fds, 0, sizeof(list_t));
  list_foreach(lchild, process->fds) {
    process_fd_t *fd = lchild->value;
    if (fd) ++(fd->refcount);
    if (fd && (fd->node.flags & FS_PIPE)) {
      pipe_t *p = fd->node.device;
      if (p && fd->node.read) ++(p->read_refcount);
      else if (p && fd->node.write) ++(p->write_refcount);
    }
    list_push_back(fds, fd);
  }
  child->fds = fds;

  uint32_t eflags = interrupt_save_disable();
  uint32_t kstack_vaddr = paging_prev_vaddr(1, FIRST_PT_VADDR);
  CHECK_UNLOCK(kstack_vaddr == 0, "No memory.", ENOMEM);
  uint32_t kstack_paddr = pmm_alloc(1);
  CHECK_UNLOCK(kstack_paddr == 0, "No memory.", ENOMEM);
  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1;
  paging_result_t res = paging_map(kstack_vaddr, kstack_paddr, flags);
  CHECK_UNLOCK(res != PAGING_OK, "Failed to map kernel stack.", res);
  child->mmap.kernel_stack_bottom = kstack_vaddr;
  child->mmap.kernel_stack_top = kstack_vaddr + PAGE_SIZE - 1;
  interrupt_restore(eflags);

  kunlock(&process_tree_lock);
  return 0;
}

#define CHECK_RESTORE(err, msg, code) if ((err)) {              \
    log_error("process", msg "\n"); interrupt_restore(eflags);  \
    paging_set_cr3(cr3); return (code);                         \
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

  uint32_t npages = page_align_up(img.text_len) >> PHYS_ADDR_OFFSET;
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

  npages = page_align_up(img.data_len) >> PHYS_ADDR_OFFSET;
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
  process->mmap.heap = img.data_vaddr + page_align_up(img.data_len);
  if (process->mmap.heap == 0)
    process->mmap.heap = img.text_vaddr + page_align_up(img.text_len);
  process->uregs.eip = img.entry;
  process->uregs.esp = process->mmap.stack_top;

  interrupt_restore(eflags);
  return 0;
}

// Set a process's argv and envp.
uint32_t process_set_env(process_t *p, char *argv[], char *envp[])
{
  uint32_t eflags = interrupt_save_disable();
  uint32_t cr3 = paging_get_cr3();
  paging_set_cr3(p->cr3);

  uint32_t argc = 0;
  for (; argv[argc]; ++argc);
  uint32_t argv_vaddr = ENV_VADDR + ((argc + 1) * sizeof(char *));
  uint32_t argv_idx = 0;
  uint32_t *ptr_ptr = (uint32_t *)ENV_VADDR;
  uint32_t ptr = argv_vaddr;
  for (; argv_idx < argc && ptr < ENV_VADDR + (PAGE_SIZE / 2); ++argv_idx) {
    ptr_ptr = (uint32_t *)(ENV_VADDR + (argv_idx * (sizeof(char *))));
    *ptr_ptr = ptr;
    u_memcpy((char *)ptr, argv[argv_idx], u_strlen(argv[argv_idx]) + 1);
    ptr += u_strlen(argv[argv_idx]) + 1;
  }
  ptr_ptr = (uint32_t *)(ENV_VADDR + (argc * (sizeof(char *))));
  *ptr_ptr = 0;

  uint32_t envc = 0;
  for (; envp[envc]; ++envc);
  uint32_t envp_vaddr = ENV_VADDR + (PAGE_SIZE / 2)
    + ((envc + 1) * sizeof(char *));
  uint32_t envp_idx = 0;
  ptr_ptr = (uint32_t *)(ENV_VADDR + (PAGE_SIZE / 2));
  ptr = envp_vaddr;
  for (; envp_idx < envc && ptr < KERNEL_START_VADDR; ++envp_idx) {
    ptr_ptr = (uint32_t *)(
      ENV_VADDR + (PAGE_SIZE / 2) + (envp_idx * (sizeof(char *)))
      );
    *ptr_ptr = ptr;
    u_memcpy((char *)ptr, envp[envp_idx], u_strlen(envp[envp_idx]) + 1);
    ptr += u_strlen(envp[envp_idx]) + 1;
  }
  ptr_ptr = (uint32_t *)(
    ENV_VADDR + (PAGE_SIZE / 2) + (envc * (sizeof(char *)))
    );
  *ptr_ptr = 0;

  paging_set_cr3(cr3);
  interrupt_restore(eflags);
  return 0;
}

// Add a process to the scheduler queue.
void process_schedule(process_t *process)
{
  uint32_t eflags = interrupt_save_disable();
  list_push_back(ready_queue, process);
  interrupt_restore(eflags);
}

// Mark a process as finished, deal with children.
void process_finish(process_t *process)
{
  if (process->ui_eip) ui_kill(process);

  klock(&process_tree_lock);
  tree_node_t *node = process->tree_node;
  list_node_t *current = node->children->head;
  list_node_t *next = current ? current->next : NULL;
  for (; current; current = next) {
    next = current->next;
    tree_node_t *tchild = current->value;
    process_t *child_process = tchild->value;

    // Threads just die, child processes become children of init.
    if (child_process->is_thread) {
      kunlock(&process_tree_lock);
      process_finish(child_process);
      klock(&process_tree_lock);
    } else {
      list_remove(node->children, current, 0);
      tree_insert(process_tree, tchild);
      kfree(current);
    }
  }
  process->is_finished = 1;

  kunlock(&process_tree_lock);
}

// Destroy a process.
static uint32_t process_destroy(process_t *process)
{
  if (process->is_thread == 0) {
    uint32_t cr3 = paging_get_cr3();
    paging_set_cr3(process->cr3);
    uint8_t res = paging_clear_user_space();
    if (res) {
      log_error("process", "Failed to clear user address space.\n");
      paging_set_cr3(cr3);
      return res;
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
    CHECK(res != PAGING_OK, "paging_unmap failed.", res);
  }

  tree_node_t *tree_node = process->tree_node;
  list_node_t *list_node = process->list_node;
  list_remove(running_list, list_node, 0);
  list_foreach(lchild, tree_node->parent->children) {
    if (lchild->value == tree_node) {
      list_remove(tree_node->parent->children, lchild, 0);
      kfree(lchild);
      break;
    }
  }

  while (process->fds->size) {
    list_node_t *head = process->fds->head;
    process_fd_t *fd = head->value;
    list_remove(process->fds, head, 0);
    kfree(head);
    if (fd == NULL) continue;
    --(fd->refcount);
    fs_close(&(fd->node));
    if (fd->refcount) continue;
    if (fd->node.flags & FS_PIPE) {
      pipe_t *p = fd->node.device;
      if (p && p->read_closed && p->write_closed) kfree(p);
    }
    kfree(fd);
  }

  kfree(process->fds);
  list_destroy(process->ui_event_queue);
  kfree(process->wd);
  kfree(tree_node);
  kfree(list_node);
  kfree(process);

  return 0;
}
