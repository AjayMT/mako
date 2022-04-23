
    ; setjmp.s
    ;
    ; Non-local jumping.
    ;
    ; Taken from ToAruOS <http://github.com/klange/toaruos>

global setjmp
global longjmp

section .text

setjmp:
    push ebp
    mov ebp, esp

    push edi
    mov edi, [ebp + 8]          ; edi points to jmp_buf

    ; save some registers
    mov [edi], eax
    mov [edi + 4], ebx
    mov [edi + 8], ecx
    mov [edi + 12], edx
    mov [edi + 16], esi

    ; save edi
    mov eax, [ebp - 4]
    mov [edi + 20], eax

    ; save ebp
    mov eax, [ebp]
    mov [edi + 24], eax

    mov eax, esp
    add eax, 12
    mov [edi + 28], eax

    mov eax, [ebp + 4]
    mov [edi + 32], eax

    pop edi
    mov eax, 0
    leave
    ret

longjmp:
    push ebp
    mov ebp, esp

    mov edi, [ebp + 8]
    mov eax, [ebp + 12]

    test eax, eax
    jne .zero
    inc eax
.zero:
    mov [edi], eax
    mov ebp, [edi + 24]
    mov esp, [edi + 28]
    push dword [edi + 32]
    mov eax, [edi]
    mov ebx, [edi + 4]
    mov ecx, [edi + 8]
    mov edx, [edi + 12]
    mov esi, [edi + 16]
    mov edi, [edi + 20]
    ret
