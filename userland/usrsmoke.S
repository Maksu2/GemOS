.code32
.section .text
.global _start

_start:
    mov $2, %eax
    mov $message, %ebx
    mov $message_len, %ecx
    int $0x80

    mov $3, %eax
    int $0x80

    mov $4, %eax
    int $0x80

    mov $42, %ebx
    mov $0, %eax
    int $0x80

1:
    jmp 1b

.section .rodata
message:
    .ascii "USRSMOKE OK\n"
message_len = . - message
