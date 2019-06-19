
    ; syscall.s
    ;
    ; System calls.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global interrupt_handler_syscall
extern syscall_handler
extern enter_usermode

section .text

interrupt_handler_syscall:
    push esp
    push eax
    push ebx
    push ecx
    push edx
    push ebp
    push esi
    push edi
    call syscall_handler
    push eax
    call enter_usermode
    jmp $
