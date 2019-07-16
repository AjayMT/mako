
    ; syscall.s
    ;
    ; System calls.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

%include "common/constants.s"

global interrupt_handler_syscall
extern syscall_handler
extern enter_usermode

section .text

interrupt_handler_syscall:
    cli
    pushad
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call syscall_handler
    push eax
    call enter_usermode
    jmp $
