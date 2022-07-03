
// syscall_nums.h
//
// Syscall numbers.
//
// Author: Ajay Tatachar <ajaymt2@illinois.edu>

#ifndef _SYSCALL_NUMS_H_
#define _SYSCALL_NUMS_H_

#define SYSCALL_EXIT              0
#define SYSCALL_FORK              1
#define SYSCALL_EXECVE            2
#define SYSCALL_MSLEEP            3
#define SYSCALL_PAGEALLOC         4
#define SYSCALL_PAGEFREE          5
#define SYSCALL_SIGNAL_REGISTER   6
#define SYSCALL_SIGNAL_RESUME     7
#define SYSCALL_SIGNAL_SEND       8
#define SYSCALL_GETPID            9
#define SYSCALL_OPEN              10
#define SYSCALL_CLOSE             11
#define SYSCALL_READ              12
#define SYSCALL_WRITE             13
#define SYSCALL_READDIR           14
#define SYSCALL_CHMOD             15
#define SYSCALL_READLINK          16
#define SYSCALL_UNLINK            17
#define SYSCALL_SYMLINK           18
#define SYSCALL_MKDIR             19
#define SYSCALL_PIPE              20
#define SYSCALL_MOVEFD            21
#define SYSCALL_CHDIR             22
#define SYSCALL_GETCWD            23
#define SYSCALL_WAIT              24
#define SYSCALL_FSTAT             25
#define SYSCALL_LSTAT             26
#define SYSCALL_LSEEK             27
#define SYSCALL_THREAD            28
#define SYSCALL_DUP               29
#define SYSCALL_THREAD_REGISTER   30
#define SYSCALL_YIELD             31
#define SYSCALL_UI_REGISTER       32
#define SYSCALL_UI_MAKE_RESPONDER 33
#define SYSCALL_UI_SPLIT          34
#define SYSCALL_UI_RESUME         35
#define SYSCALL_UI_SWAP_BUFFERS   36
#define SYSCALL_UI_WAIT           37
#define SYSCALL_UI_YIELD          38
#define SYSCALL_RENAME            39
#define SYSCALL_RESOLVE           40
#define SYSCALL_SYSTIME           41
#define SYSCALL_PRIORITY          42

#endif /* _SYSCALL_NUMS_H_ */
