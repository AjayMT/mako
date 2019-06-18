
    ; io.s
    ;
    ; Serial port I/O interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global outb
global outw
global outl
global inb
global inw
global inl

section .text

    ; outb -- send a byte to an I/O port
    ; stack: [esp + 8] the byte
    ;        [esp + 4] the I/O port
    ;        [esp    ] return address
outb:
    mov al, [esp + 8]
    mov dx, [esp + 4]
    out dx, al
    ret

    ; inb -- get a byte from an I/O port
    ; stack: [esp + 4] the address of the I/O port
    ;        [esp    ] the return address
inb:
    mov dx, [esp + 4]
    in  al, dx
    ret

    ; outw -- send a WORD to an I/O port.
outw:
    mov ax, [esp + 8]
    mov dx, [esp + 4]
    out dx, ax
    ret

    ; inw -- get a WORD from an I/O port.
inw:
    mov dx, [esp + 4]
    in ax, dx
    ret

    ; outl -- send a DWORD to an I/O port.
outl:
    mov eax, [esp + 8]
    mov dx, [esp + 4]
    out dx, eax
    ret

    ; inl -- get a DWORD from an I/O port.
inl:
    mov dx, [esp + 4]
    in eax, dx
    ret
