
[bits 32]

global _start
extern main

section .text
align 4
_start:
    push dword 0
    push dword 0
    call main
    jmp $
