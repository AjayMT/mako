
    ; process.s
    ;
    ; Process management and user mode.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global enter_usermode

section .text
enter_usermode:
    cli
    mov eax, [esp + 4]          ; store address of registers struct in eax

    ; restore registers
    mov ebx, [eax + 4]
    mov ecx, [eax + 8]
    mov edx, [eax + 12]
    mov ebp, [eax + 16]
    mov esi, [eax + 20]
    mov edi, [eax + 24]

    push dword [eax + 28]       ; ss
    push dword [eax + 32]       ; esp
    push dword [eax + 36]       ; eflags
    push dword [eax + 40]       ; segment selector
    push dword [eax + 44]       ; eip

    push ecx
    mov cx, [eax + 28]
    mov ds, cx
    mov gs, cx
    mov es, cx
    mov fs, cx
    pop ecx

    mov eax, [eax]              ; restore eax

    iret
