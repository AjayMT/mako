
    ; idt.s
    ;
    ; Interrupt descriptor table interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global idt_load

section .text

    ; idt_load -- Load the Interrupt Descriptor Table.
    ; stack: [esp + 4] the address to load the IDT into
    ;        [esp    ] the return address
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret
