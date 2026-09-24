/*
 * GemOS Kernel Entry (Assembly)
 *
 * Stage 2 jumps here (0x100000) in 32-bit protected mode with a flat
 * memory model, ESP = 0x9F000 and EBX pointing to the boot info block
 * (kernel/include/boot_info.h). This must stay the first code in the image:
 * stage 2 also reads the kernel header below from the first sector.
 */

    .code32
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern kernel_main

_start:
    jmp 1f

    /* Kernel header, read by stage 2 before loading the rest */
    .align 4
    .ascii "GEMK"                   /* magic (KERNEL_MAGIC in loader.asm) */
    .long __kernel_image_size       /* bytes to copy to 0x100000 */

1:
    cld

    /* Zero the BSS. The loader copies only the kernel file and RAM contents
     * at boot are undefined. */
    mov edi, offset __bss_start
    mov ecx, offset __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    /* Run on the guarded task 0 stack (kernel/memory/kstack.c) instead of
     * the loader's stack at 0x9F000, which has nothing below it. */
    mov esp, dword ptr [kstack_boot_top]

    /* kernel_main(const boot_info_t *) */
    push ebx
    call kernel_main

    /* If kernel_main returns (it shouldn't), halt */
.hang:
    cli
    hlt
    jmp .hang
