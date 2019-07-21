
    ; crt0.s
    ;
    ; C "runtime" initialisation.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global _start
extern _init_sig
extern _init_stdio
extern _init_thread
extern exit
extern environ
extern main
extern _init

    ARGV_VADDR equ 0xBFFFF000

section .text
count_args:
    mov ebx, [esp + 4]
    mov eax, 0
.check:
    cmp dword [ebx], 0
    jne .increment
    ret
.increment:
    inc eax
    add ebx, 4
    jmp .check

_start:
    mov ebp, 0
    push ebp
    push ebp
    mov ebp, esp

    call _init
    call _init_sig
    call _init_thread
    call _init_stdio
    push dword environ
    push dword ARGV_VADDR
    call count_args
    push eax
    call main
    push eax
    call exit
