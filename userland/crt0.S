.code32
.section .text
.global _start
.extern main

_start:
    call main
    mov %eax, %ebx
    mov $0, %eax
    int $0x80

1:
    jmp 1b
