
// syscall.c
//
// System calls.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#include "process.h"
#include "interrupt.h"
#include "pit.h"
#include "fs.h"
#include "pipe.h"
#include "elf.h"
#include "paging.h"
#include "pmm.h"
#include "klock.h"
#include "kheap.h"
#include "constants.h"
#include "../common/errno.h"
#include "util.h"
#include "../libc/sys/stat.h"
#include "ui.h"
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

static void execve_set_env(char *argv[], char *envp[])
{
  uint32_t argc = 0;
  for (; argv[argc]; ++argc);
  uint32_t argv_vaddr = PROCESS_ENV_VADDR + ((argc + 1) * sizeof(char *));
  uint32_t argv_idx = 0;
  uint32_t *ptr_ptr = (uint32_t *)PROCESS_ENV_VADDR;
  uint32_t ptr = argv_vaddr;
  for (; argv_idx < argc && ptr < PROCESS_ENV_VADDR + (PAGE_SIZE / 2); ++argv_idx) {
    ptr_ptr = (uint32_t *)(PROCESS_ENV_VADDR + (argv_idx * (sizeof(char *))));
    *ptr_ptr = ptr;
    u_memcpy((char *)ptr, argv[argv_idx], u_strlen(argv[argv_idx]) + 1);
    ptr += u_strlen(argv[argv_idx]) + 1;
  }
  ptr_ptr = (uint32_t *)(PROCESS_ENV_VADDR + (argc * (sizeof(char *))));
  *ptr_ptr = 0;

  uint32_t envc = 0;
  for (; envp[envc]; ++envc);
  uint32_t envp_vaddr = PROCESS_ENV_VADDR + (PAGE_SIZE / 2)
    + ((envc + 1) * sizeof(char *));
  uint32_t envp_idx = 0;
  ptr_ptr = (uint32_t *)(PROCESS_ENV_VADDR + (PAGE_SIZE / 2));
  ptr = envp_vaddr;
  for (; envp_idx < envc && ptr < KERNEL_START_VADDR; ++envp_idx) {
    ptr_ptr = (uint32_t *)(
      PROCESS_ENV_VADDR + (PAGE_SIZE / 2) + (envp_idx * (sizeof(char *)))
      );
    *ptr_ptr = ptr;
    u_memcpy((char *)ptr, envp[envp_idx], u_strlen(envp[envp_idx]) + 1);
    ptr += u_strlen(envp[envp_idx]) + 1;
  }
  ptr_ptr = (uint32_t *)(
    PROCESS_ENV_VADDR + (PAGE_SIZE / 2) + (envc * (sizeof(char *)))
    );
  *ptr_ptr = 0;
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
    char **kargv = kmalloc((argc + 2) * sizeof(char *));
    if (kargv == NULL) { current->uregs.eax = -ENOMEM; return; }
    kargv[0] = kmalloc(u_strlen(path) + 1);
    if (kargv[0] == NULL) { current->uregs.eax = -ENOMEM; return; }
    u_memcpy(kargv[0], path, u_strlen(path) + 1);
    for (uint32_t i = 0; i < argc; ++i) {
      char *arg = kmalloc(u_strlen(argv[i]) + 1);
      if (arg == NULL) { current->uregs.eax = -ENOMEM; return; }
      u_memcpy(arg, argv[i], u_strlen(argv[i]) + 1);
      kargv[i + 1] = arg;
    }
    kargv[argc + 1] = NULL;

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
    execve_set_env(kargv, kenvp);

    for (uint32_t i = 0; kargv[i]; ++i) kfree(kargv[i]);
    kfree(kargv);
    for (uint32_t i = 0; kenvp[i]; ++i) kfree(kenvp[i]);
    kfree(kenvp);
    kfree(p.text);
    kfree(p.data);
    kfree(buf);
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
  for (
    ;
    buf_idx < line_len;
    buf_idx += u_strlen((char *)buf + buf_idx) + 1
    )
  {
    new_argv[new_argv_idx] = (char *)buf + buf_idx;
    ++new_argv_idx;
  }
  new_argv[new_argv_idx] = path;
  ++new_argv_idx;
  for (uint32_t i = 0; i <= argc; ++i) {
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
  process_t *current = process_current();
  current->uregs.eax = 0;
  uint64_t wake_time = pit_get_time() + duration;
  uint32_t res = process_sleep(current, wake_time);
  if (res) { current->uregs.eax = -res; return; }
  disable_interrupts();
  current->in_kernel = 0;
  process_unschedule(current);
  process_switch_next();
}

static void syscall_exit(uint32_t status)
{
  process_t *current = process_current();
  current->exited = 1;
  current->exit_status = status;
  // process_kill switches to the next process after killing
  // the current process.
  process_kill(current);
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
  uint32_t eflags = interrupt_save_disable();
  u_memcpy(
    &(current->uregs),
    &(current->saved_signal_regs),
    sizeof(process_registers_t)
    );
  current->current_signal = 0;
  interrupt_restore(eflags);
}

static void syscall_signal_send(uint32_t pid, uint32_t signum)
{
  uint8_t err = process_signal_pid(pid, signum);
  if (err) {
    process_current()->uregs.eax = -ESRCH; return;
  }
  process_current()->uregs.eax = 0;
}

static void syscall_getpid()
{ process_current()->uregs.eax = process_current()->pid; }

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

  current->uregs.eax = -EMFILE;
  klock(&current->fd_lock);
  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i) {
    if (current->fds[i] != NULL) continue;
    current->fds[i] = fd;
    current->uregs.eax = i;
    break;
  }
  kunlock(&current->fd_lock);
  if ((int32_t)current->uregs.eax == -EMFILE) kfree(fd);
}

#define CHECK_FDNUM                                                     \
  if (fdnum >= MAX_PROCESS_FDS || current->fds[fdnum] == NULL) {        \
    kunlock(&current->fd_lock); current->uregs.eax = -EBADF; return;    \
  }

static void syscall_close(int32_t fdnum)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];
  --(fd->refcount);
  if (fd->refcount == 0) {
    fs_close(&(fd->node));
    kfree(fd);
  }
  current->fds[fdnum] = NULL;
  kunlock(&current->fd_lock);
  current->uregs.eax = 0;
}

static void syscall_read(uint32_t fdnum, uint8_t *buf, uint32_t size)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];
  int32_t res = fs_read(&(fd->node), fd->offset, size, buf);
  if (res < 0) { kunlock(&current->fd_lock); current->uregs.eax = res; return; }
  fd->offset += res;
  kunlock(&current->fd_lock);
  current->uregs.eax = res;
}

static void syscall_write(uint32_t fdnum, uint8_t *buf, uint32_t size)
{
  process_t *current = process_current();
  klock(&current->fd_lock);
  CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];
  int32_t res = fs_write(&(fd->node), fd->offset, size, buf);
  if (res < 0) { kunlock(&current->fd_lock); current->uregs.eax = res; return; }
  fd->offset += res;
  kunlock(&current->fd_lock);
  current->uregs.eax = res;
}

static void syscall_readdir(int32_t fdnum, struct dirent *ent, uint32_t index)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];
  struct dirent *res = fs_readdir(&(fd->node), index);
  kunlock(&current->fd_lock);
  if (res == NULL) { current->uregs.eax = -ENOENT; return; }
  u_memcpy(ent, res, sizeof(struct dirent));
  kfree(res);
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

  process_fd_t *read_fd = kmalloc(sizeof(process_fd_t));
  if (read_fd == NULL) { current->uregs.eax = -ENOMEM; return; }
  u_memset(read_fd, 0, sizeof(process_fd_t));
  read_fd->refcount = 1;

  process_fd_t *write_fd = kmalloc(sizeof(process_fd_t));
  if (write_fd == NULL) { current->uregs.eax = -ENOMEM; return; }
  u_memset(write_fd, 0, sizeof(process_fd_t));
  write_fd->refcount = 1;

  uint32_t res = pipe_create(&(read_fd->node), &(write_fd->node));
  if (res) { current->uregs.eax = -res; return; }

  current->uregs.eax = -EMFILE;
  klock(&current->fd_lock);
  int32_t rfd = -1, wfd = -1;
  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i) {
    if (current->fds[i] != NULL) continue;
    if (rfd == -1) rfd = i;
    else if (wfd == -1) { wfd = i; break; }
  }
  if (rfd != -1 && wfd != -1) {
    current->fds[rfd] = read_fd;
    *read_fdnum = rfd;
    current->fds[wfd] = write_fd;
    *write_fdnum = wfd;
    current->uregs.eax = 0;
  }
  kunlock(&current->fd_lock);

  if (current->uregs.eax != 0) {
    fs_close(&(read_fd->node)); fs_close(&(write_fd->node));
    kfree(read_fd); kfree(write_fd);
  }
}

static void syscall_movefd(uint32_t fdn1, uint32_t fdn2)
{
  process_t *current = process_current();

  klock(&current->fd_lock);
  if (fdn1 >= MAX_PROCESS_FDS || current->fds[fdn1] == NULL || fdn2 >= MAX_PROCESS_FDS) {
    kunlock(&current->fd_lock); current->uregs.eax = -EBADF; return;
  }
  process_fd_t *fd2 = current->fds[fdn2];
  --(fd2->refcount);
  if (fd2->refcount == 0) {
    fs_close(&(fd2->node));
    kfree(fd2);
  }
  current->fds[fdn2] = current->fds[fdn1];
  current->fds[fdn1] = NULL;
  kunlock(&current->fd_lock);
  current->uregs.eax = 0;
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

  char *rpath;
  res = resolve_path(&rpath, path);
  if (res) { current->uregs.eax = -res; return; }
  uint32_t len = u_strlen(rpath);
  kfree(current->wd);
  current->wd = kmalloc(len + 1);
  u_memcpy(current->wd, rpath, len + 1);
  kfree(rpath);
  current->uregs.eax = 0;
}

static void syscall_getcwd(char *buf, uint32_t bufsize)
{
  if (u_strlen(process_current()->wd) + 1 < bufsize)
    bufsize = u_strlen(process_current()->wd) + 1;
  u_memcpy(buf, process_current()->wd, bufsize);
}

static void syscall_wait(uint32_t pid)
{
  process_t *current = process_current();
  uint8_t err = process_wait_pid(current, pid);
  if (err) { current->uregs.eax = -ESRCH; return; }
  disable_interrupts();
  current->in_kernel = 0;
  process_unschedule(current);
  process_switch_next();
}

static void syscall_fstat(uint32_t fdnum, struct stat *st)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];
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
  kunlock(&current->fd_lock);
  st->st_blksize = 1024;
  current->uregs.eax = 0;
}

static void syscall_lstat(char *path, struct stat *st)
{
  process_t *current = process_current();
  fs_node_t node; u_memset(&node, 0, sizeof(fs_node_t));
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

static void syscall_lseek(uint32_t fdnum, int32_t offset, uint32_t whence)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *pfd = current->fds[fdnum];
  if (whence == 1) pfd->offset += offset;
  else if (whence == 2) pfd->offset = pfd->node.length + offset;
  else pfd->offset = offset;
  kunlock(&current->fd_lock);
  current->uregs.eax = pfd->offset;
}

static void syscall_thread(uint32_t eip, uint32_t data)
{
  process_t *current = process_current();
  process_t *child = kmalloc(sizeof(process_t));
  if (child == NULL) { current->uregs.eax = -ENOMEM; return; }

  uint32_t res = process_fork(child, current, 1);
  if (res) { current->uregs.eax = -res; return; }

  child->uregs.ebp = child->mmap.stack_top;
  child->uregs.esp = child->uregs.ebp;
  child->uregs.eip = child->thread_start;
  child->uregs.edi = eip;
  child->uregs.ecx = data;
  child->uregs.eax = 0;
  current->uregs.eax = child->pid;
  process_schedule(child);
}

static void syscall_dup(uint32_t fdnum)
{
  process_t *current = process_current();
  klock(&current->fd_lock); CHECK_FDNUM;
  process_fd_t *fd = current->fds[fdnum];

  current->uregs.eax = -EMFILE;
  for (uint32_t i = 0; i < MAX_PROCESS_FDS; ++i) {
    if (current->fds[i] != NULL) continue;
    current->fds[i] = fd;
    ++(fd->refcount);
    current->uregs.eax = i;
    break;
  }
  kunlock(&current->fd_lock);
}

static void syscall_thread_register(uint32_t start)
{ process_current()->thread_start = start; }

static void syscall_yield()
{
  disable_interrupts();
  process_current()->in_kernel = 0;
  process_switch_next();
}

static void syscall_ui_make_responder(uint32_t buf)
{
  process_t *current = process_current();
  current->uregs.eax = -ui_make_responder(current, buf);
}

static void syscall_ui_swap_buffers()
{
  process_t *current = process_current();
  current->uregs.eax = -ui_swap_buffers(current);
}

static void syscall_ui_next_event(uint32_t buf)
{
  process_t *current = process_current();
  current->uregs.eax = -ui_next_event(current, buf);
}

static void syscall_ui_poll_events()
{
  process_t *current = process_current();
  current->uregs.eax = ui_poll_events(current);
}

static void syscall_ui_yield()
{
  process_t *current = process_current();
  current->uregs.eax = -ui_yield(current);
}

static void syscall_rename(char *old, char *new)
{ process_current()->uregs.eax = fs_rename(old, new); }

static void syscall_resolve(char *outpath, char *inpath, size_t len)
{
  process_t *current = process_current();
  char *rpath;
  uint32_t res = resolve_path(&rpath, inpath);
  if (res) { current->uregs.eax = -res; return; }
  size_t l = u_strlen(rpath) + 1;
  if (l > len) l = len;
  u_memcpy(outpath, rpath, l);
  kfree(rpath);
  current->uregs.eax = 0;
}

static void syscall_systime()
{ process_current()->uregs.eax = pit_get_time(); }

static void syscall_priority(int32_t prio)
{
  process_t *current = process_current();
  if (prio < 0) { current->uregs.eax = current->priority; return; }
  if (prio > MAX_PROCESS_PRIORITY) prio = MAX_PROCESS_PRIORITY;
  uint32_t eflags = interrupt_save_disable();
  process_unschedule(current);
  current->priority = prio;
  current->uregs.eax = prio;
  process_schedule(current);
  interrupt_restore(eflags);
}

static void syscall_ui_set_wallpaper(const char *path)
{
  process_t *current = process_current();
  current->uregs.eax = -ui_set_wallpaper(path);
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
  syscall_yield,
  syscall_ui_make_responder,
  syscall_ui_swap_buffers,
  syscall_ui_next_event,
  syscall_ui_poll_events,
  syscall_ui_yield,
  syscall_rename,
  syscall_resolve,
  syscall_systime,
  syscall_priority,
  syscall_ui_set_wallpaper,
};

process_registers_t *syscall_handler(cpu_state_t cs, stack_state_t ss)
{
  update_current_process_registers(cs, ss);

  process_t *current = process_current();
  current->in_kernel = 1;
  uint32_t syscall_num = cs.eax;
  uint32_t a1 = cs.edi;
  uint32_t a2 = cs.ecx;
  uint32_t a3 = cs.edx;
  uint32_t a4 = cs.esi;

  enable_interrupts();
  syscall_table[syscall_num](a1, a2, a3, a4);

  disable_interrupts();
  current->in_kernel = 0;

  if (current->next_signal && current->current_signal == 0) {
    u_memcpy(&(current->saved_signal_regs), &(current->uregs), sizeof(process_registers_t));
    current->current_signal = current->next_signal;
    current->next_signal = 0;
    current->uregs.eip = current->signal_eip;
    current->uregs.edi = current->current_signal;
  }

  return &(current->uregs);
}
