
    ; tss.s
    ;
    ; Task State Segment interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global tss_load_set

section .text
tss_load_set:
    mov ax, [esp + 4]
    ltr ax
    ret
