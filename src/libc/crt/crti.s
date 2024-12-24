
    ; crti.s
    ;
    ; C "runtime" initialisation.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global _init
global _fini

section .init
_init:
    push ebp
    mov ebp, esp

section .fini
_fini:
    push ebp
    mov ebp, esp
