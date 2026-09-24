/*
 * GemOS Kernel Entry (Assembly)
 *
 * Stage 2 jumps here (0x100000) in 32-bit protected mode with a flat
 * memory model, ESP = 0x9F000 and EBX pointing to the boot info block
 * (kernel/include/boot_info.h). This must stay the first code in the image.
 */

    .code32
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern kernel_main

_start:
    cld

    /* Zero the BSS. The loader copies only the kernel file and RAM contents
     * at boot are undefined. */
    mov edi, offset __bss_start
    mov ecx, offset __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    /* kernel_main(const boot_info_t *) */
    push ebx
    call kernel_main

    /* If kernel_main returns (it shouldn't), halt */
.hang:
    cli
    hlt
    jmp .hang
