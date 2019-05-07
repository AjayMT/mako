
    ; io.s
    ;
    ; Serial port I/O interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global outb
global inb

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
