/*
 * Entry point of the C programs. The kernel starts a process with argc at
 * the top of its stack, then argv[0..argc-1] and a NULL pointer
 * (include/gemos/user_api.h). The value main returns is the exit code.
 */
.code32
.section .text
.global _start
.extern main

_start:
    mov (%esp), %eax        /* argc */
    lea 4(%esp), %edx       /* argv */
    push %edx
    push %eax
    call main
    mov %eax, %ebx
    mov $0, %eax            /* SYS_exit */
    int $0x80

1:
    jmp 1b
