OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
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

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
    }
}
