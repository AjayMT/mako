
    ; _syscall.s
    ;
    ; Syscall interrupt interface
    ;
    ; Author: Ajay Tatachar <ajay.tatachar@gmail.com>

global _syscall0
global _syscall1
global _syscall2
global _syscall3
global _syscall4

section .text
_syscall0:
    mov eax, [esp + 4]
    int 0x80
    ret

_syscall1:
    push ebp
    mov ebp, esp
    push ebx
    mov ebx, [ebp + 12]
    mov eax, [ebp + 8]
    int 0x80
    pop ebx
    leave
    ret

_syscall2:
    push ebp
    mov ebp, esp
    push ebx
    mov ecx, [ebp + 16]
    mov ebx, [ebp + 12]
    mov eax, [ebp + 8]
    int 0x80
    pop ebx
    leave
    ret

_syscall3:
    push ebp
    mov ebp, esp
    push ebx
    mov edx, [ebp + 20]
    mov ecx, [ebp + 16]
    mov ebx, [ebp + 12]
    mov eax, [ebp + 8]
    int 0x80
    pop ebx
    leave
    ret

_syscall4:
    push ebp
    mov ebp, esp
    push ebx
    push edi
    mov edi, [ebp + 24]
    mov edx, [ebp + 20]
    mov ecx, [ebp + 16]
    mov ebx, [ebp + 12]
    mov eax, [ebp + 8]
    int 0x80
    pop ebx
    pop edi
    leave
    ret
