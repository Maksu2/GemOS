/*
 * context_switch.S - Preemptive scheduler IRQ0 stub
 *
 * GAS AT&T syntax (.code32), compiled via $(CC) $(CFLAGS) -c
 *
 * This stub replaces isr32 in the IDT after scheduler_init() calls
 * idt_set_gate(32, scheduler_irq0_stub, ...).
 *
 * Stack layout on entry:
 *
 *   [ESP+0]  EIP    (CPU)
 *   [ESP+4]  CS     (CPU)
 *   [ESP+8]  EFLAGS (CPU)
 *   [ESP+12] ESP, [ESP+16] SS — only when IRQ0 interrupted Ring 3; the
 *            stub leaves them alone and iret pops them again.
 *
 * After push $0 / push $32 / pusha / push DS — matches registers_t in isr.h:
 *
 *   [ESP+0]  DS
 *   [ESP+4]  EDI     <- pusha last push  (lowest addr in pusha block)
 *   [ESP+8]  ESI
 *   [ESP+12] EBP
 *   [ESP+16] ESP_pre (snapshot before pusha, ignored by popa)
 *   [ESP+20] EBX
 *   [ESP+24] EDX
 *   [ESP+28] ECX
 *   [ESP+32] EAX     <- pusha first push (highest addr in pusha block)
 *   [ESP+36] int_no  (32)
 *   [ESP+40] err_code (0)
 *   [ESP+44] EIP     (CPU)
 *   [ESP+48] CS      (CPU)
 *   [ESP+52] EFLAGS  (CPU)
 *
 * Total frame: 56 bytes.
 *
 * scheduler_tick(current_esp) receives the address of the DS field (= frame
 * base) and returns new_esp — either the same address (no switch) or a
 * different task's saved frame base. It switches only when IRQ0 interrupted
 * Ring 3 (see kernel/scheduler.c). isr_common_stub saves the same frame, so
 * a task can be resumed through either stub.
 *
 * Context switch happens at the single instruction:
 *   mov %eax, %esp
 */

.code32
.section .text

.extern scheduler_tick

.global scheduler_irq0_stub
scheduler_irq0_stub:
    push $0             /* err_code placeholder */
    push $32            /* int_no = IRQ0 */

    pusha               /* EAX, ECX, EDX, EBX, ESP_pre, EBP, ESI, EDI */

    /* Save DS and load kernel data segment */
    mov %ds, %ax
    push %eax           /* only %ax matters when the frame is restored */

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call scheduler_tick(current_esp).
     * ESP currently points at DS field = base of saved frame.
     * We push that value as the argument; scheduler_tick returns new_esp in EAX. */
    push %esp
    call scheduler_tick
    add $4, %esp        /* clean up argument (ESP is about to be overwritten anyway) */

    /* Switch to new (or same) task's stack */
    mov %eax, %esp      /* <<< context switch happens here */

    /* Restore DS from new task's frame */
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa                /* EDI, ESI, EBP, (skip ESP), EBX, EDX, ECX, EAX */
    add $8, %esp        /* skip int_no + err_code */
    iret                /* EIP, CS, EFLAGS -> resume new task */
