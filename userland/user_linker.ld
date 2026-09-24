OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

/* Code and read-only data in one segment (R+X, mapped read-only), data
 * and BSS in another (R+W) that starts on its own page. The ELF headers
 * are not loaded: code starts at 0x02000000. The Makefile links with -n,
 * so in the file the data follows the code without padding to a page.
 * Without PAE the CPU has no no-execute bit, so data pages stay
 * executable. */
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x02000000;

    .text ALIGN(4) :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data :
    {
        *(.data)
        *(.data.*)
    } :data

    .bss ALIGN(4) (NOLOAD) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
    }
}
