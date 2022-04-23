
    ; paging.s
    ;
    ; Paging interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global paging_set_cr3
global paging_invalidate_pte
global paging_get_cr3

section .text

    ; paging_set_cr3 -- Set the page directory.
    ; stack: [esp + 4] the physical address of the page directory
    ;        [esp    ] return address
paging_set_cr3:
    mov eax, [esp + 4]
    and eax, 0xFFFFF000
    mov cr3, eax
    ret

    ; paging_invalidate_pte -- Remove a PTE from the TLB.
    ; stack: [esp + 4] the virtual address whose PTE to remove
    ;        [esp    ] return address
paging_invalidate_pte:
    mov eax, [esp + 4]
    invlpg [eax]
    ret

paging_get_cr3:
    mov eax, cr3
    ret
