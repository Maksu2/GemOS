/*
 * FAULTS.ELF, self-test image only (make selftest).
 *
 * Raises the CPU exception picked by its test number (the numbers are in
 * include/gemos/selftest_abi.h; kernel/selftest.c writes the number after
 * the marker below). The kernel has to end this process, and only this
 * process. If the exception does not come, the program exits with
 * GEMOS_SELFTEST_SURVIVED, or GEMOS_SELFTEST_NOT_RAISED when the CPU only
 * flagged it.
 */
#include <gemos/selftest_abi.h>

.code32

.section .rodata
    .ascii GEMOS_SELFTEST_MARKER
test_number:
    .long 0xFFFFFFFF

    .align 4
raise_table:                            /* in test number order */
    .long raise_divide
    .long raise_debug
    .long raise_breakpoint
    .long raise_overflow
    .long raise_bound
    .long raise_invalid_opcode
    .long raise_invalid_tss
    .long raise_not_present
    .long raise_stack_segment
    .long raise_gp_privileged
    .long raise_gp_kernel_gate
    .long raise_pf_code_write
    .long raise_pf_kernel_read
    .long raise_pf_kernel_jump
    .long raise_pf_stack
    .long raise_x87
    .long raise_simd
raise_table_end:

bound_range:
    .long 0, 10
one:
    .float 1.0
zero:
    .float 0.0

.section .text
.global _start
_start:
    mov test_number, %eax
    cmp $((raise_table_end - raise_table) / 4), %eax
    jae survived
    jmp *raise_table(, %eax, 4)

raise_divide:
    xor %edx, %edx
    mov $1, %eax
    xor %ecx, %ecx
    div %ecx
    jmp survived

raise_debug:                            /* trap after the nop */
    pushf
    orl $0x100, (%esp)                  /* EFLAGS.TF */
    popf
    nop
    jmp survived

raise_breakpoint:
    int3
    jmp survived

raise_overflow:
    mov $0x7F, %al
    add $1, %al                         /* signed overflow: OF = 1 */
    into
    jmp survived

raise_bound:
    mov $100, %eax
    bound %eax, bound_range             /* 100 is outside [0, 10] */
    jmp survived

raise_invalid_opcode:
    ud2
    jmp survived

raise_invalid_tss:
    /* With EFLAGS.NT set, iret returns to the task in the TSS back link,
     * which is the null selector. */
    pushf
    orl $0x4000, (%esp)                 /* EFLAGS.NT */
    popf
    iret
    jmp survived

raise_not_present:
    mov $GEMOS_SELFTEST_NP_SELECTOR, %ax
    mov %ax, %ds
    jmp survived

raise_stack_segment:
    mov $GEMOS_SELFTEST_NP_SELECTOR, %ax
    mov %ax, %ss
    jmp survived

raise_gp_privileged:
    cli                                 /* needs IOPL 3 */
    jmp survived

raise_gp_kernel_gate:
    int $0x81                           /* the kernel's yield gate is DPL 0 */
    jmp survived

raise_pf_code_write:
    movl $0, _start                     /* code pages are read-only */
    jmp survived

raise_pf_kernel_read:
    mov 0x00100000, %eax                /* the kernel image */
    jmp survived

raise_pf_kernel_jump:
    mov $0x00100000, %eax
    call *%eax
    jmp survived

raise_pf_stack:
1:  push %eax                           /* until below the 8 KB stack */
    jmp 1b

raise_x87:
    fninit
    sub $4, %esp
    fnstcw (%esp)
    andw $~0x0004, (%esp)               /* unmask divide by zero */
    fldcw (%esp)
    fld1
    fdivs zero
    fwait                               /* the pending exception is raised here */
    jmp survived

raise_simd:
    sub $4, %esp
    stmxcsr (%esp)
    andl $~0x0200, (%esp)               /* unmask divide by zero */
    ldmxcsr (%esp)
    movss one, %xmm0
    xorps %xmm1, %xmm1
    divss %xmm1, %xmm0
    stmxcsr (%esp)                      /* no #XM: was it flagged? */
    mov (%esp), %eax
    and $0x0204, %eax                   /* ZM, ZE */
    cmp $0x0004, %eax                   /* unmasked and flagged */
    jne survived
    mov $GEMOS_SELFTEST_NOT_RAISED, %ebx
    mov $0, %eax                        /* SYS_exit */
    int $0x80

survived:
    mov $GEMOS_SELFTEST_SURVIVED, %ebx
    mov $0, %eax                        /* SYS_exit */
    int $0x80
1:  jmp 1b
