
    ; crtn.s
    ;
    ; C "runtime" initialisation.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

section .init
    pop ebp
    ret

section .fini
    pop ebp
    ret
