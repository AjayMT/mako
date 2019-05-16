
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
    FLAGS equ MOD_ALIGN | MEMINFO ; multiboot flags
    MAGIC equ 0x1BADB002          ; magic constant for multiboot
    CHECKSUM equ -(MAGIC + FLAGS) ; magic + checksum + flags = 0

    KERNEL_PT_CONFIG equ 0xB         ; present, writable, write through cache
    KERNEL_PDT_IDENTITY_MAP equ 0x8B ; ^ and global

section .data
align 4096
kernel_pt:
    times 1024 dd 0
kernel_pdt:
    dd KERNEL_PDT_IDENTITY_MAP
    times 1023 dd 0

section .data
align 4
grub_magic_number:
    dd 0
grub_multiboot_info:
    dd 0

section .bss
align 4
kernel_stack:
    resb KERNEL_STACK_SIZE

section .text
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

loader:                         ; entry point called by GRUB
    mov ecx, (grub_magic_number - KERNEL_START_VADDR)
    mov [ecx], eax
    mov ecx, (grub_multiboot_info - KERNEL_START_VADDR)
    mov [ecx], ebx

init_kernel_pdt:
    mov ecx, (kernel_pdt - KERNEL_START_VADDR + (KERNEL_PDT_IDX * 4))
    mov edx, (kernel_pt - KERNEL_START_VADDR)
    or  edx, KERNEL_PT_CONFIG
    mov [ecx], edx

init_kernel_pt:
    mov eax, (kernel_pt - KERNEL_START_VADDR)
    mov ecx, KERNEL_PT_CONFIG
.mapping_loop:
    mov [eax], ecx
    add eax, 4
    add ecx, 0x1000
    cmp ecx, (kernel_end - KERNEL_START_VADDR)
    jle .mapping_loop

enable_paging:
    mov ecx, (kernel_pdt - KERNEL_START_VADDR)
    and ecx, 0xFFFFF000         ; discard all but the upper 20 bits
    or  ecx, 0x8                ; enable write-through-cache
    mov cr3, ecx                ; load pdt

    mov ecx, cr4                ; read current config
    or  ecx, 0x10               ; enable 4MB pages
    mov cr4, ecx                ; write config

    mov ecx, cr0                ; read current config
    or  ecx, 0x80000000         ; enable paging
    mov cr0, ecx                ; write config

    lea ecx, [higher_half]
    jmp ecx                     ; jump to the higher half

higher_half:                    ; at this point we are using the page table
    mov [kernel_pdt], DWORD 0
    invlpg [0]                  ; stop identity mapping the first 4 MB

fwd_kmain:
    mov  esp, kernel_stack + KERNEL_STACK_SIZE
    call kmain
hang:
    jmp hang
