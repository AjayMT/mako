
// syscall.c
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <process/process.h>
#include <interrupt/interrupt.h>
#include <fs/fs.h>
#include <elf/elf.h>
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

static void syscall_exit(uint32_t status)
{
  log_debug("syscall", "exiting %x\n", status);
  process_finish(process_current());
  process_switch_next();
}

static syscall_t syscall_table[] = {
  syscall_exit,
  syscall_fork,
  syscall_execve
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
