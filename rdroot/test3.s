[bits 32]

global _start

section .data
random_stuff: dd 0

section .bss
more_stuff: resb 4096

section .text
_start:
  mov eax, 0xf00dbabe
  push eax
  pop ebx
  jmp _start
