.code32
.section .text

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    ljmp $0x08, $gdt_flush_done
gdt_flush_done:
    ret

.global tss_flush
tss_flush:
    mov 4(%esp), %ax
    ltr %ax
    ret
