
    ; fpu.s
    ;
    ; FPU context handling.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global fpu_init

section .text

fpu_init:
    clts
    mov eax, cr0
    and eax, ~4
    or eax, 2
    mov cr0, eax

    mov eax, cr4
    or eax, (3 << 9)
    mov cr4, eax

    fninit
    ret
