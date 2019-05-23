
    ; paging.s
    ;
    ; Paging interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global paging_set_directory
global invalidate_page_table_entry

section .text

    ; paging_set_directory -- Set the page directory.
    ; stack: [esp + 4] the physical address of the page directory
    ;        [esp    ] return address
paging_set_directory:
    mov eax, [esp + 4]
    and eax, 0xFFFFF000
    mov cr3, eax
    ret

    ; invalidate_page_table_entry -- Remove a PTE from the TLB.
    ; stack: [esp + 4] the virtual address whose PTE to remove
    ;        [esp    ] return address
invalidate_page_table_entry:
    mov eax, [esp + 4]
    invlpg [eax]
    ret
