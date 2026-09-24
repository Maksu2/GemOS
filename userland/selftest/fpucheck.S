/*
 * FPUCHECK.ELF, self-test image only (make selftest).
 *
 * Loads its own x87 and SSE state (control word, three constants on the
 * x87 stack, MXCSR, a pattern in XMM0-XMM7), then checks it for
 * GEMOS_FPUCHECK_MS milliseconds, giving up the CPU between the checks.
 * kernel/selftest.c runs two copies with different values (test numbers 1
 * and 2) next to the GUI task, which uses the x87 in the font engine: if a
 * task switch did not save and restore the FPU state, a copy would see
 * registers of another task. Test number 0 exits at once; the self-test
 * uses it to check that an unmodified image still loads.
 *
 * Exit codes are in include/gemos/selftest_abi.h.
 */
#include <gemos/selftest_abi.h>

.code32

.section .rodata
    .ascii GEMOS_SELFTEST_MARKER
test_number:
    .long 0xFFFFFFFF

control_words:                          /* exceptions masked, 64-bit precision */
    .word 0x077F                        /* round down */
    .word 0x0B7F                        /* round up */
    .align 4
mxcsr_values:                           /* exceptions masked */
    .long 0x3F80                        /* round down */
    .long 0x5F80                        /* round up */

.section .bss
    .align 16
expected_x87:
    .space 30                           /* ST0-ST2, 10 bytes each */
    .align 16
expected_sse:
    .space 128                          /* XMM0-XMM7 */
scratch:
    .space 16
set_index:
    .space 4                            /* 0 or 1 */
start_ms:
    .space 4

.section .text
.global _start
_start:
    mov test_number, %eax
    test %eax, %eax
    jz passed
    cmp $2, %eax
    ja bad_argument
    dec %eax
    mov %eax, set_index

    fninit
    fldcw control_words(, %eax, 2)
    test %eax, %eax
    jnz 1f
    fldpi
    fldl2e
    fldln2
    jmp 2f
1:  fldlg2
    fldl2t
    fld1
2:  fld %st(0)
    fstpt expected_x87
    fld %st(1)
    fstpt expected_x87 + 10
    fld %st(2)
    fstpt expected_x87 + 20

    ldmxcsr mxcsr_values(, %eax, 4)
    lea expected_sse, %edi
    mov $32, %ecx
    mov %eax, %edx
    shl $16, %edx
    or $0xA5000000, %edx                /* 0xA5, set, word number */
3:  mov %edx, (%edi)
    add $4, %edi
    inc %edx
    loop 3b
    movups expected_sse + 0, %xmm0
    movups expected_sse + 16, %xmm1
    movups expected_sse + 32, %xmm2
    movups expected_sse + 48, %xmm3
    movups expected_sse + 64, %xmm4
    movups expected_sse + 80, %xmm5
    movups expected_sse + 96, %xmm6
    movups expected_sse + 112, %xmm7

    mov $4, %eax                        /* SYS_ticks_ms */
    int $0x80
    mov %eax, start_ms

check_loop:
    mov $5000, %ecx                     /* some work: the timer may preempt it */
4:  loop 4b
    mov $1, %eax                        /* SYS_yield */
    int $0x80

    fnstcw scratch
    movzwl scratch, %eax
    mov set_index, %edx
    movzwl control_words(, %edx, 2), %edx
    cmp %edx, %eax
    jne x87_changed
    fnstsw %ax
    and $0x3800, %ax
    cmp $0x2800, %ax                    /* TOP = 5 after three loads */
    jne x87_changed
    fld %st(0)
    fstpt scratch
    lea expected_x87, %esi
    call compare10
    jne x87_changed
    fld %st(1)
    fstpt scratch
    lea expected_x87 + 10, %esi
    call compare10
    jne x87_changed
    fld %st(2)
    fstpt scratch
    lea expected_x87 + 20, %esi
    call compare10
    jne x87_changed

    stmxcsr scratch
    mov set_index, %edx
    mov mxcsr_values(, %edx, 4), %edx
    cmp %edx, scratch
    jne sse_changed
    movups %xmm0, scratch
    lea expected_sse + 0, %esi
    call compare16
    jne sse_changed
    movups %xmm1, scratch
    lea expected_sse + 16, %esi
    call compare16
    jne sse_changed
    movups %xmm2, scratch
    lea expected_sse + 32, %esi
    call compare16
    jne sse_changed
    movups %xmm3, scratch
    lea expected_sse + 48, %esi
    call compare16
    jne sse_changed
    movups %xmm4, scratch
    lea expected_sse + 64, %esi
    call compare16
    jne sse_changed
    movups %xmm5, scratch
    lea expected_sse + 80, %esi
    call compare16
    jne sse_changed
    movups %xmm6, scratch
    lea expected_sse + 96, %esi
    call compare16
    jne sse_changed
    movups %xmm7, scratch
    lea expected_sse + 112, %esi
    call compare16
    jne sse_changed

    mov $4, %eax                        /* SYS_ticks_ms */
    int $0x80
    sub start_ms, %eax
    cmp $GEMOS_FPUCHECK_MS, %eax
    jb check_loop

passed:
    mov $GEMOS_FPUCHECK_OK, %ebx
    jmp exit
x87_changed:
    mov $GEMOS_FPUCHECK_X87_CHANGED, %ebx
    jmp exit
sse_changed:
    mov $GEMOS_FPUCHECK_SSE_CHANGED, %ebx
    jmp exit
bad_argument:
    mov $GEMOS_FPUCHECK_BAD_ARGUMENT, %ebx
exit:
    mov $0, %eax                        /* SYS_exit */
    int $0x80
5:  jmp 5b

/* Compare 10 or 16 bytes at scratch with (%esi); ZF = 1 if equal. */
compare10:
    lea scratch, %edi
    mov $10, %ecx
    repe cmpsb
    ret
compare16:
    lea scratch, %edi
    mov $16, %ecx
    repe cmpsb
    ret
