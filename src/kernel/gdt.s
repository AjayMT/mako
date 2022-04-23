
    ; gdt.s
    ;
    ; Global descriptor table interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global gdt_load

SEGMENT_SELECTOR_KERNEL_CS equ 0x08
SEGMENT_SELECTOR_KERNEL_DS equ 0x10

section .text

    ; gdt_load -- Load the global descriptor table, and load segment
    ;             selectors into data and code segment registers.
    ; stack: [esp + 4] the address to load the GDT into
    ;        [esp    ] the return address
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]

    ; load cs by doing a "far jump"
    jmp SEGMENT_SELECTOR_KERNEL_CS:.load_segment_selectors

.load_segment_selectors:
    mov ax, SEGMENT_SELECTOR_KERNEL_DS
    mov ds, ax                ; -
    mov es, ax                ;  |
    mov ss, ax                ;  |-> Loading segment selectors for
    mov fs, ax                ;  |   data registers.
    mov gs, ax                ; -
    ret
