/*
 * GemOS Linker Script
 * 
 * Defines memory layout for the kernel.
 * Kernel is loaded at 1MB (0x100000) by the bootloader.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB */
    . = 0x100000;
    
    /* Code section */
    .text ALIGN(4K) :
    {
        *(.text)
        *(.text.*)
    }
    
    /* Read-only data */
    .rodata ALIGN(4K) :
    {
        *(.rodata)
        *(.rodata.*)
    }
    
    /* Initialized data */
    .data ALIGN(4K) :
    {
        *(.data)
        *(.data.*)
    }
    
    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /* End of kernel */
    __kernel_end = .;
    
    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
    }
}
