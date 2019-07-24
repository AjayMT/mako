
    ; boot.s
    ;
    ; Kernel loader.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

%include "common/constants.s"

global loader                   ; entry symbol for ELF
global kernel_stack             ; address of the kernel stack
extern kmain                    ; C entry point
extern kernel_start             ; start address of the kernel exported from link.ld
extern kernel_end               ; end address of the kernel exported from link.ld

    MOD_ALIGN equ 1               ; align loaded modules on page boundaries
    MEMINFO equ 2                 ; provide memory map
    GRAPHICS equ 4                ; Set graphics mode
    FLAGS equ MOD_ALIGN | MEMINFO | GRAPHICS
    MAGIC equ 0x1BADB002          ; magic constant for multiboot
    CHECKSUM equ -(MAGIC + FLAGS) ; magic + checksum + flags = 0

section .data
align 4096
kernel_pd:
    ; First entry in the page directory is the identity map.
    ; Identity mapping is necessary so that the instruction pointer
    ; doesn't point to an invalid address after paging is enabled.
    dd 10000011b
    times (KERNEL_PD_IDX - 1) dd 0
    ; This entry maps the kernel.
    dd 10000011b
    times (1023 - KERNEL_PD_IDX) dd 0

section .bss
align 4
kernel_stack:
    resb KERNEL_STACK_SIZE

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0

    ; Request linear graphics mode.
    dd 0
    dd 0
    dd 0
    dd 32

section .text
loader:                         ; entry point called by GRUB
    ; We don't need to do anything in here for now.

enable_paging:
    mov ecx, (kernel_pd - KERNEL_START_VADDR)
    and ecx, 0xFFFFF000         ; discard all but the upper 20 bits
    mov cr3, ecx                ; load pdt

    mov ecx, cr4                ; read current config
    or  ecx, 0x10               ; enable 4MB pages
    mov cr4, ecx                ; write config

    mov ecx, cr0                ; read current config
    or  ecx, 0x80010000         ; enable paging and write protection
    mov cr0, ecx                ; write config

    lea ecx, [higher_half]
    jmp ecx                     ; absolute jump to the higher half

higher_half:                    ; at this point we are using the page table
    mov [kernel_pd], DWORD 0    ; stop identity mapping the first 4 MB
    invlpg [0]                  ; flush TLB

fwd_kmain:
    mov  esp, kernel_stack + KERNEL_STACK_SIZE
    push kernel_end
    push kernel_start
    push kernel_pd
    push eax
    add  ebx, KERNEL_START_VADDR
    push ebx
    call kmain
    jmp $                       ; loop indefinitely
