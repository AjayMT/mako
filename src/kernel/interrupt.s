
    ; interrupt.s
    ;
    ; Interrupt handling interface for Mako.
    ;
    ; Author: Ajay Tatachar <ajaymt2@illinois.edu>

global enable_interrupts
global disable_interrupts
global interrupt_save_disable
global interrupt_restore

extern forward_interrupt

    ; Declare an interupt handler that discards the error code.
%macro no_error_code_handler 1
global interrupt_handler_%1
    interrupt_handler_%1:
    cli
    push dword 0                ; Push 0 as the error code.
    push dword %1               ; Push the interrupt number.
    jmp  common_interrupt_handler
%endmacro

    ; Declare an interrupt handler that preserves the error code
    ; (which is already on the stack).
%macro error_code_handler 1
global interrupt_handler_%1
    interrupt_handler_%1:
    cli
    push dword %1               ; Push the interrupt number
    jmp  common_interrupt_handler
%endmacro

section .text

    ; Interrupt handlers
    ; Protected mode exceptions
    no_error_code_handler 0     ; Divide by zero
    no_error_code_handler 1     ; Debug exceptopn
    no_error_code_handler 2     ; Non-maskable interrupt
    no_error_code_handler 3     ; Breakpoint exception
    no_error_code_handler 4     ; "Into detected overflow"
    no_error_code_handler 5     ; Out of bounds exception
    no_error_code_handler 6     ; Invalid opcode exception
    no_error_code_handler 7     ; No coprocessor exception
    error_code_handler 8        ; Double fault
    no_error_code_handler 9     ; Coprocessor segment overrun
    error_code_handler 10       ; Bad TSS
    error_code_handler 11       ; Segment not present
    error_code_handler 12       ; Stack fault
    error_code_handler 13       ; General protection fault
    error_code_handler 14       ; Page fault
    no_error_code_handler 15    ; Unknown interrupt exception
    no_error_code_handler 16    ; Coprocessor fault
    error_code_handler 17       ; Alignment check exception
    no_error_code_handler 18    ; Machine check exception
    no_error_code_handler 19    ; SIMD floating point exception

    ; IRQs
    no_error_code_handler 32
    no_error_code_handler 33
    no_error_code_handler 34
    no_error_code_handler 35
    no_error_code_handler 36
    no_error_code_handler 37
    no_error_code_handler 38
    no_error_code_handler 39
    no_error_code_handler 40
    no_error_code_handler 41
    no_error_code_handler 42
    no_error_code_handler 43
    no_error_code_handler 44
    no_error_code_handler 45
    no_error_code_handler 46
    no_error_code_handler 47

    ; Common parts of the interrupt handlers.
    ; Pushes register state to the stack, forwards the interrupt
    ; to an interrupt_handler_t and restores register state.
common_interrupt_handler:
    pushad
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call forward_interrupt

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popad
    add esp, 8
    sti
    iret

enable_interrupts:
    sti
    ret
disable_interrupts:
    cli
    ret

interrupt_save_disable:
    pushfd
    pop eax
    cli
    ret
interrupt_restore:
    mov eax, [esp + 4]
    push eax
    popfd
    ret
