
    ; ringbuffer.s
    ;
    ; Ring buffer for device I/O, pipes, etc.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

%include "constants.s"

extern ringbuffer_check_read
extern ringbuffer_wait_read
extern ringbuffer_finish_read
extern ringbuffer_check_write
extern ringbuffer_wait_write
extern ringbuffer_finish_write
extern debug

%macro ringbuffer_io 1
global ringbuffer_%1
    ringbuffer_%1:
    mov ebx, [esp + 4]          ; ringbuffer_t *rb
    mov ecx, [esp + 8]          ; uint32_t size
    mov edx, [esp + 12]         ; uint8_t *buf

    ; Check if there is stuff to read/space to write.
    .check_%1:
    pushfd
    cli
    push ecx
    push edx
    push ebx
    call ringbuffer_check_%1
    pop ebx
    pop edx
    pop ecx
    or eax, [ebx + 12]
    cmp eax, 0
    jne .finish_%1

    ; Push process registers.
    ; esp needs to be increased by 4 * 4 to match its value
    ; at `.check_%1`
    push .check_%1
    push dword KERNEL_CS
    push dword 0x202
    push esp
    push dword KERNEL_DS
    push edi
    push esi
    push ebp
    push edx
    push ecx
    push ebx
    push eax

    ; Push the ringbuffer pointer and call the wait function.
    push ebx
    call ringbuffer_wait_%1

    ; Perform the read/write operation and wake waiting readers/writers
    .finish_%1:
    push edx
    push ecx
    push ebx
    call ringbuffer_finish_%1
    pop ebx
    pop ecx
    pop edx
    popfd
    ret
%endmacro

section .text
    ringbuffer_io read
    ringbuffer_io write
