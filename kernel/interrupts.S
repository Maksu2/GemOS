/*
 * interrupts.S - Assembly stubs for Interrupt Service Routines
 */

.code32
.section .text

/* Defined in isr.c */
.extern isr_handler

/* Common ISR stub */
isr_common_stub:
    /* Save all registers */
    pusha
    
    /* Save data segment */
    mov %ds, %ax
    push %eax
    
    /* Load kernel data segment descriptor */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    /* Call C handler */
    push %esp
    call isr_handler
    add $4, %esp

    /* If the handler returned a replacement ESP, switch before restoring. */
    test %eax, %eax
    jz 1f
    mov %eax, %esp
1:
    
    /* Restore data segment */
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    /* Restore registers */
    popa
    
    /* Cleanup error code and ISR number pushed in macros */
    add $8, %esp
    
    /* Return from interrupt */
    iret

/* Macro for exception without error code */
.macro ISR_NOERRCODE num
    .global isr\num
    isr\num:
        cli
        push $0          /* Push dummy error code */
        push $\num       /* Push interrupt number */
        jmp isr_common_stub
.endm

/* Macro for exception with error code */
.macro ISR_ERRCODE num
    .global isr\num
    isr\num:
        cli
        /* Error code is already pushed by CPU */
        push $\num
        jmp isr_common_stub
.endm

/* Define first 32 ISRs */
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31


/* IRQs (ISR 32-47) */
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47

/* System call gate */
ISR_NOERRCODE 128
