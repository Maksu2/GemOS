/*
 * GemOS Kernel Entry (Assembly)
 * 
 * Minimal assembly stub that sets up the environment and calls kernel_main().
 * Using GNU Assembler (GAS) syntax for i386.
 */

    .code32
    .intel_syntax noprefix
    .section .text
    .global _start
    .extern kernel_main

_start:
    /* We're already in 32-bit Protected Mode with flat memory model */
    /* Stack is set up by bootloader at 0x90000 */

    /* Clear direction flag */
    cld
    
    /* Call C kernel entry point */
    call kernel_main
    
    /* If kernel_main returns (it shouldn't), halt */
.hang:
    cli
    hlt
    jmp .hang
