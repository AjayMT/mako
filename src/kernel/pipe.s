
; pipe.s
;
; Pipe for IPC.
;
; Author: Ajay Tatachar <ajaymt2@illinois.edu>

%include "constants.s"

extern process_switch_next

section .text
global pipe_suspend
pipe_suspend:
    mov eax, [esp + 4]  ; process_registers_t *regs
    mov edx, [esp + 8]  ; uint32_t updated

    mov [eax], dword 1
    mov [eax + 4], ebx
    mov [eax + 8], ecx
    mov [eax + 12], edx
    mov [eax + 16], ebp
    mov [eax + 20], esi
    mov [eax + 24], edi
    mov [eax + 28], dword KERNEL_DS
    mov [eax + 32], esp
    mov [eax + 36], dword 0x202
    mov [eax + 40], dword KERNEL_CS
    mov [eax + 44], dword .resume
    xor eax, eax

.resume:
    and eax, edx ; if (just_resumed && updated)
    cmp eax, 0
    je .switch
    sti
    ret

.switch:
    call process_switch_next
