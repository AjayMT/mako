
    ; klock.s
    ;
    ; Kernel locks.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

%include "constants.s"

global klock
extern klock_wait

section .text
klock:
    pushfd
    cli
    push eax
    mov eax, [esp + 12]
    cmp DWORD [eax], 1
    jne .acquire_lock
    pop eax
    push klock
    push DWORD KERNEL_CS
    push DWORD 0x202
    push esp                    ; needs to be adjusted by 4 * 4
    push DWORD KERNEL_DS
    push edi
    push esi
    push ebp
    push edx
    push ecx
    push ebx
    push eax
    call klock_wait
    ret
.acquire_lock:
    mov DWORD [eax], 1
    pop eax
    popfd
    ret
