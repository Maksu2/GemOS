OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

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

    .data ALIGN(4) :
    {
        *(.data)
        *(.data.*)
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
