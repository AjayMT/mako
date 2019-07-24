
// syscall.c
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include <process/process.h>
#include <interrupt/interrupt.h>
#include <pit/pit.h>
#include <fs/fs.h>
#include <pipe/pipe.h>
#include <elf/elf.h>
#include <paging/paging.h>
#include <pmm/pmm.h>
#include <klock/klock.h>
#include <kheap/kheap.h>
#include <common/constants.h>
#include <common/errno.h>
#include <debug/log.h>
#include <util/util.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "syscall.h"

typedef void (*syscall_t)();

static void syscall_fork()
{
  process_t *current = process_current();
  process_t *child = kmalloc(sizeof(process_t));
  if (child == NULL) { current->uregs.eax = -ENOMEM; return; }

  uint32_t res = process_fork(child, current, 0);
  if (res) { current->uregs.eax = -res; return; }

  child->uregs.eax = 0;
  current->uregs.eax = child->pid;
  process_schedule(child);
}

static void syscall_execve(char *path, char *argv[], char *envp[])
{
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

  if (rsize < 2 || buf[0] != '#' || buf[1] != '!') {
    kfree(buf); current->uregs.eax = -ENOEXEC; return;
  }

  uint32_t argc = 0;
  for (; argv[argc]; ++argc);
  uint32_t line_len = 0;
  for (; buf[line_len] && buf[line_len] != '\n'; ++line_len);
  for (uint32_t i = 0; i < line_len; ++i)
    if (buf[i] == ' ' && i > 2) buf[i] = '\0';
  buf[line_len] = '\0';

  char **new_argv = kmalloc((argc + line_len) * (sizeof(char *)));
  if (new_argv == NULL) { current->uregs.eax = -ENOMEM; return; }
  uint32_t buf_idx = u_strlen((char *)buf) + 1;
  uint32_t new_argv_idx = 0;
  for (; buf_idx < line_len; buf_idx += u_strlen((char *)buf) + 1) {
    new_argv[new_argv_idx] = (char *)buf + buf_idx;
    ++new_argv_idx;
  }
  new_argv[new_argv_idx] = path;
  ++new_argv_idx;
  for (uint32_t i = 0; i < argc; ++i) {
    new_argv[new_argv_idx] = argv[i];
    ++new_argv_idx;
  }

  uint8_t buf_offset = buf[2] == ' ' ? 3 : 2;
  syscall_execve((char *)buf + buf_offset, new_argv, envp);
  kfree(new_argv);
  kfree(buf);
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
  process_t *current = process_current();
  process_finish(current);
  current->exited = 1;
  current->exit_status = status;
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
  current->signal_pending = 0;
}

static void syscall_signal_send(uint32_t pid, uint32_t signum)
{
  process_t *target = process_from_pid(pid);
  if (target == NULL) {
    process_current()->uregs.eax = -ESRCH; return;
  }
  process_signal(target, signum);
  process_current()->uregs.eax = 0;
}

static void syscall_getpid()
{ process_current()->uregs.eax = process_current()->pid; }

static list_node_t *find_fd(uint32_t fdnum)
{
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();
  list_node_t *lnode = NULL;
  list_foreach(fdnode, current->fds) {
    if (fdnum == 0) lnode = fdnode;
    --fdnum;
  }
  interrupt_restore(eflags);
  return lnode;
}

static void syscall_open(char *path, uint32_t flags, uint32_t mode)
{
  process_t *current = process_current();
  int32_t res = 0;
  if (flags & O_CREAT) {
    res = fs_create(path, mode);
    if (res < 0) { current->uregs.eax = res; return; }
  }

  process_fd_t *fd = kmalloc(sizeof(process_fd_t));
  if (fd == NULL) { current->uregs.eax = -ENOMEM; return; }
  u_memset(fd, 0, sizeof(process_fd_t));
  res = fs_open_node(&(fd->node), path, flags);
  if (res) {
    kfree(fd); current->uregs.eax = -res; return;
  }
  fd->refcount = 1;

  list_push_back(current->fds, fd);
  current->uregs.eax = current->fds->size - 1;
}

static void syscall_close(int32_t fdnum)
{
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();
  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) {
    current->uregs.eax = -EBADF;
    interrupt_restore(eflags);
    return;
  }

  process_fd_t *fd = lnode->value;
  --(fd->refcount);
  fs_close(&(fd->node));
  if (fd->is_pipe) {
    pipe_t *p = fd->node.device;
    if (&(fd->node) == p->read_node && p->write_closed) kfree(p);
    if (&(fd->node) == p->write_node && p->read_closed) kfree(p);
  }
  if (fd->refcount == 0) kfree(fd);
  lnode->value = NULL;

  current->uregs.eax = 0;
  interrupt_restore(eflags);
}

static void syscall_read(uint32_t fdnum, uint8_t *buf, uint32_t size)
{
  process_t *current = process_current();
  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *fd = lnode->value;
  int32_t res = fs_read(&(fd->node), fd->offset, size, buf);
  if (res < 0) { current->uregs.eax = res; return; }
  fd->offset += res;
  current->uregs.eax = res;
}

static void syscall_write(uint32_t fdnum, uint8_t *buf, uint32_t size)
{
  process_t *current = process_current();

  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *fd = lnode->value;
  int32_t res = fs_write(&(fd->node), fd->offset, size, buf);
  if (res < 0) { current->uregs.eax = res; return; }

  fd->offset += res;
  current->uregs.eax = res;
}

static void syscall_readdir(int32_t fdnum, struct dirent *ent, uint32_t index)
{
  process_t *current = process_current();
  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *fd = lnode->value;
  struct dirent *res = fs_readdir(&(fd->node), index);
  if (res == NULL) { current->uregs.eax = -ENOENT; return; }
  u_memcpy(ent, res, sizeof(struct dirent));
  current->uregs.eax = 0;
}

static void syscall_chmod(char *path, uint32_t mode)
{
  process_t *current = process_current();
  fs_node_t node;
  uint32_t res = fs_open_node(&node, path, O_RDONLY);
  if (res) { current->uregs.eax = -res; return; }
  current->uregs.eax = fs_chmod(&node, mode);
}

static void syscall_readlink(char *path, char *buf, uint32_t bufsize)
{
  process_t *current = process_current();
  fs_node_t node;
  uint32_t res = fs_open_node(&node, path, O_RDONLY);
  if (res) { current->uregs.eax = -res; return; }
  current->uregs.eax = fs_readlink(&node, buf, bufsize);
}

static void syscall_unlink(char *path)
{ process_current()->uregs.eax = fs_unlink(path); }

static void syscall_symlink(char *path1, char *path2)
{ process_current()->uregs.eax = fs_symlink(path1, path2); }

static void syscall_mkdir(char *path, uint32_t mode)
{ process_current()->uregs.eax = fs_mkdir(path, mode); }

static void syscall_pipe(uint32_t *read_fdnum, uint32_t *write_fdnum)
{
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();

  process_fd_t *read_fd = kmalloc(sizeof(process_fd_t));
  if (read_fd == NULL) { current->uregs.eax = -ENOMEM; return; }
  u_memset(read_fd, 0, sizeof(process_fd_t));
  read_fd->is_pipe = 1;

  process_fd_t *write_fd = kmalloc(sizeof(process_fd_t));
  if (write_fd == NULL) { current->uregs.eax = -ENOMEM; return; }
  u_memset(write_fd, 0, sizeof(process_fd_t));
  write_fd->is_pipe = 1;

  uint32_t res = pipe_create(&(read_fd->node), &(write_fd->node));
  if (res) { current->uregs.eax = -res; return; }

  list_push_back(current->fds, read_fd);
  *read_fdnum = current->fds->size - 1;
  list_push_back(current->fds, write_fd);
  *write_fdnum = current->fds->size - 1;

  current->uregs.eax = 0;
  interrupt_restore(eflags);
}

static void syscall_movefd(uint32_t fdn1, uint32_t fdn2)
{
  process_t *current = process_current();
  uint32_t eflags = interrupt_save_disable();

  list_node_t *lnode = find_fd(fdn1);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *fd1 = lnode->value;
  list_node_t *lnode2 = find_fd(fdn2);
  if (lnode2 == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *fd2 = lnode2->value;

  syscall_close(fdn2);
  if (current->uregs.eax != 0) {
    interrupt_restore(eflags); return;
  }

  lnode->value = NULL;
  lnode2->value = fd1;

  interrupt_restore(eflags);
}

static void syscall_chdir(char *path)
{
  process_t *current = process_current();
  fs_node_t node;
  uint32_t res = fs_open_node(&node, path, O_RDONLY);
  if (res) { current->uregs.eax = -res; return; }
  if ((node.flags & FS_DIRECTORY) == 0) {
    current->uregs.eax = -ENOTDIR; return;
  }

  u_memcpy(current->wd, path, u_strlen(path));
  current->uregs.eax = 0;
}

static void syscall_getcwd(char *buf, uint32_t bufsize)
{
  if (u_strlen(process_current()->wd) + 1 < bufsize)
    bufsize = u_strlen(process_current()->wd) + 1;
  u_memcpy(buf, process_current()->wd, bufsize);
}

static void syscall_wait(uint32_t pid, struct _wait_result *w)
{
  process_t *current = process_current();
  process_t *target = process_from_pid(pid);
  if (target == NULL) { current->uregs.eax = -ESRCH; return; }
  while (target->is_finished == 0);
  w->exited = target->exited;
  w->status = target->exit_status;
  w->signal = target->signal_pending;
  current->uregs.eax = pid;
}

static void syscall_fstat(uint32_t fdnum, struct stat *st)
{
  process_t *current = process_current();
  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }

  process_fd_t *fd = lnode->value;
  u_memset(st, 0, sizeof(struct stat));
  st->st_dev = fd->node.flags;
  st->st_ino = fd->node.inode;
  st->st_mode = fd->node.mask;
  st->st_nlink = 1;
  st->st_uid = fd->node.uid;
  st->st_gid = fd->node.gid;
  st->st_size = fd->node.length;
  st->st_atime = fd->node.atime;
  st->st_mtime = fd->node.mtime;
  st->st_ctime = fd->node.ctime;
  st->st_blksize = 1024;
  current->uregs.eax = 0;
}

static void syscall_lstat(char *path, struct stat *st)
{
  process_t *current = process_current();
  fs_node_t node;
  uint32_t res = fs_open_node(&node, path, O_NOFOLLOW);
  if (res) { current->uregs.eax = -res; return; }
  u_memset(st, 0, sizeof(struct stat));
  st->st_dev = node.flags;
  st->st_ino = node.inode;
  st->st_mode = node.mask;
  st->st_nlink = 1;
  st->st_uid = node.uid;
  st->st_gid = node.gid;
  st->st_size = node.length;
  st->st_atime = node.atime;
  st->st_mtime = node.mtime;
  st->st_ctime = node.ctime;
  st->st_blksize = 1024;
  current->uregs.eax = 0;
}

static void syscall_lseek(uint32_t fd, int32_t offset, uint32_t whence)
{
  process_t *current = process_current();
  list_node_t *lnode = find_fd(fd);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  process_fd_t *pfd = lnode->value;
  if (whence == 1) pfd->offset += offset;
  else if (whence == 2) pfd->offset = pfd->node.length + offset;
  else pfd->offset = offset;
  current->uregs.eax = pfd->offset;
}

static void syscall_thread(uint32_t eip, uint32_t data)
{
  process_t *current = process_current();
  process_t *child = kmalloc(sizeof(process_t));
  if (child == NULL) { current->uregs.eax = -ENOMEM; return; }

  uint32_t res = process_fork(child, current, 1);
  if (res) { current->uregs.eax = -res; return; }

  child->uregs.eip = child->thread_start;
  child->uregs.ebx = eip;
  child->uregs.ecx = data;
  child->uregs.eax = 0;
  current->uregs.eax = child->pid;
  process_schedule(child);
}

static void syscall_dup(uint32_t fdnum)
{
  process_t *current = process_current();
  list_node_t *lnode = find_fd(fdnum);
  if (lnode == NULL) { current->uregs.eax = -EBADF; return; }
  uint32_t eflags = interrupt_save_disable();
  process_fd_t *fd1 = lnode->value;
  ++(fd1->refcount);
  list_push_back(current->fds, fd1);
  current->uregs.eax = current->fds->size - 1;
  interrupt_restore(eflags);
}

static void syscall_thread_register(uint32_t start)
{ process_current()->thread_start = start; }

static void syscall_yield()
{ process_switch_next(); }

static syscall_t syscall_table[] = {
  syscall_exit,
  syscall_fork,
  syscall_execve,
  syscall_msleep,
  syscall_pagealloc,
  syscall_pagefree,
  syscall_signal_register,
  syscall_signal_resume,
  syscall_signal_send,
  syscall_getpid,
  syscall_open,
  syscall_close,
  syscall_read,
  syscall_write,
  syscall_readdir,
  syscall_chmod,
  syscall_readlink,
  syscall_unlink,
  syscall_symlink,
  syscall_mkdir,
  syscall_pipe,
  syscall_movefd,
  syscall_chdir,
  syscall_getcwd,
  syscall_wait,
  syscall_fstat,
  syscall_lstat,
  syscall_lseek,
  syscall_thread,
  syscall_dup,
  syscall_thread_register,
  syscall_yield
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
