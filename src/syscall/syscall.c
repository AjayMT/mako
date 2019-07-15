
// syscall.c
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <process/process.h>
#include <interrupt/interrupt.h>
#include <pit/pit.h>
#include <fs/fs.h>
#include <elf/elf.h>
#include <paging/paging.h>
#include <pmm/pmm.h>
#include <klock/klock.h>
#include <kheap/kheap.h>
#include <common/constants.h>
#include <common/errno.h>
#include <debug/log.h>
#include <util/util.h>
#include "syscall.h"

typedef void (*syscall_t)();

static void syscall_fork()
{
  process_t *current = process_current();
  process_t *child = kmalloc(sizeof(process_t));
  if (child == NULL) { current->uregs.eax = -ENOMEM; return; }

  uint32_t res = process_fork(child, current);
  if (res) { current->uregs.eax = -res; return; }

  child->uregs.eax = 0;
  current->uregs.eax = child->pid;
  process_schedule(child);
}

static void syscall_execve(char *path, char *argv[], char *envp[])
{
  // TODO Handle shebang scripts.

  process_t *current = process_current();
  fs_node_t node;
  uint32_t res = fs_open_node(&node, path, O_RDONLY);
  if (res) { current->uregs.eax = -res; return; }
  uint8_t *buf = kmalloc(node.length);
  if (buf == NULL) { current->uregs.eax = -ENOMEM; return; }
  uint32_t rsize = fs_read(&node, 0, node.length, buf);
  if (rsize != node.length) { current->uregs.eax = -EAGAIN; return; }

  if (rsize >= 4 && elf_is_valid(buf)) {
    uint32_t argc = 0;
    for (; argv[argc]; ++argc);
    char **kargv = kmalloc((argc + 1) * sizeof(char *));
    if (kargv == NULL) { current->uregs.eax = -ENOMEM; return; }
    for (uint32_t i = 0; i < argc; ++i) {
      char *arg = kmalloc(u_strlen(argv[i]) + 1);
      if (arg == NULL) { current->uregs.eax = -ENOMEM; return; }
      u_memcpy(arg, argv[i], u_strlen(argv[i]) + 1);
      kargv[i] = arg;
    }
    kargv[argc] = NULL;

    uint32_t envc = 0;
    for (; envp[envc]; ++envc);
    char **kenvp = kmalloc((envc + 1) * sizeof(char *));
    if (kenvp == NULL) { current->uregs.eax = -ENOMEM; return; }
    for (uint32_t i = 0; i < envc; ++i) {
      char *env = kmalloc(u_strlen(envp[i]) + 1);
      if (env == NULL) { current->uregs.eax = -ENOMEM; return; }
      u_memcpy(env, envp[i], u_strlen(envp[i]) + 1);
      kenvp[i] = env;
    }
    kenvp[envc] = NULL;

    process_image_t p;
    u_memset(&p, 0, sizeof(process_image_t));
    res = elf_load(&p, buf);
    if (res) { current->uregs.eax = -res; return; }
    res = process_load(current, p);
    if (res) { current->uregs.eax = -res; return; }
    res = process_set_env(current, kargv, kenvp);
    if (res) { current->uregs.eax = -res; return; }

    for (uint32_t i = 0; kargv[i]; ++i) kfree(kargv[i]);
    kfree(kargv);
    for (uint32_t i = 0; kenvp[i]; ++i) kfree(kenvp[i]);
    kfree(kenvp);
    kfree(p.text);
    kfree(p.data);
    kfree(buf);
    u_memcpy(current->name, node.name, PROCESS_NAME_LEN);
    return;
  }
}

static void syscall_msleep(uint32_t duration)
{
  uint32_t eflags = interrupt_save_disable();
  process_t *current = process_current();
  current->uregs.eax = 0;
  uint32_t wake_time = pit_get_time() + duration;
  if (duration > 8) {
    current->is_running = 0;
    uint32_t res = process_sleep(current, wake_time);
    if (res) { current->uregs.eax = -res; return; }
  }
  interrupt_restore(eflags);

  while (wake_time > pit_get_time());
}

static void syscall_exit(uint32_t status)
{
  process_finish(process_current());
  process_switch_next();
}

static void syscall_pagealloc(uint32_t npages)
{
  if (npages == 0) return;
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();
  uint32_t vaddr = paging_next_vaddr(npages, current->mmap.heap);
  if (vaddr == 0) { current->uregs.eax = 0; return; }

  page_table_entry_t flags; u_memset(&flags, 0, sizeof(flags));
  flags.rw = 1; flags.user = 1;
  uint32_t err = 0;
  uint32_t i = 0;
  for (; i < npages; ++i) {
    uint32_t paddr = pmm_alloc(1);
    if (paddr == 0) { err = ENOMEM; break; }
    paging_result_t res = paging_map(vaddr + (i * PAGE_SIZE), paddr, flags);
    if (res != PAGING_OK) { err = res; break; }
  }

  if (err) {
    for (uint32_t k = 0; k < i; ++k) {
      uint32_t paddr = paging_get_paddr(vaddr + (k * PAGE_SIZE));
      if (paddr) pmm_free(paddr, 1);
      paging_unmap(vaddr + (k * PAGE_SIZE));
    }
    current->uregs.eax = 0;
    return;
  }

  current->uregs.eax = vaddr;
  interrupt_restore(eflags);
}

static void syscall_pagefree(uint32_t vaddr, uint32_t npages)
{
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();

  for (uint32_t i = 0; i < npages; ++i) {
    uint32_t paddr = paging_get_paddr(vaddr + (i * PAGE_SIZE));
    if (paddr) { pmm_free(paddr, 1); }
    paging_result_t res = paging_unmap(vaddr + (i * PAGE_SIZE));
    if (res != PAGING_OK) { current->uregs.eax = -res; return; }
  }

  current->uregs.eax = 0;
  interrupt_restore(eflags);
}

static void syscall_signal_register(uint32_t eip)
{ process_current()->signal_eip = eip; }

static void syscall_signal_resume()
{
  process_t *current = process_current();
  u_memcpy(
    &(current->uregs),
    &(current->saved_signal_regs),
    sizeof(process_registers_t)
    );
}

static void syscall_signal_send(uint32_t pid, uint32_t signum)
{
  process_t *target = process_from_pid(pid);
  process_signal(target, signum);
}

static syscall_t syscall_table[] = {
  syscall_exit,
  syscall_fork,
  syscall_execve,
  syscall_msleep,
  syscall_pagealloc,
  syscall_pagefree,
  syscall_signal_register,
  syscall_signal_resume,
  syscall_signal_send
};

process_registers_t *syscall_handler(cpu_state_t cs, stack_state_t ss)
{
  update_current_process_registers(cs, ss);

  process_t *current = process_current();
  current->in_kernel = 1;
  uint32_t syscall_num = cs.eax;
  uint32_t a1 = cs.ebx;
  uint32_t a2 = cs.ecx;
  uint32_t a3 = cs.edx;
  uint32_t a4 = cs.esi;

  enable_interrupts();
  syscall_table[syscall_num](a1, a2, a3, a4);

  disable_interrupts();
  current->in_kernel = 0;
  return &(current->uregs);
}

int32_t syscall0(uint32_t num)
{
  int32_t ret;
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t syscall1(uint32_t num, uint32_t a1)
{
  int32_t ret;
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t syscall2(uint32_t num, uint32_t a1, uint32_t a2)
{
  int32_t ret;
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3)
{
  int32_t ret;
  asm volatile ("movl %0, %%edx" : : "r"(a3));
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
int32_t syscall4(
  uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4
  )
{
  int32_t ret;
  asm volatile ("movl %0, %%esi" : : "r"(a4));
  asm volatile ("movl %0, %%edx" : : "r"(a3));
  asm volatile ("movl %0, %%ecx" : : "r"(a2));
  asm volatile ("movl %0, %%ebx" : : "r"(a1));
  asm volatile ("movl %0, %%eax" : : "r"(num));
  asm volatile ("int $0x80");
  asm volatile ("movl %%eax, %0" : "=r"(ret));
  return ret;
}
