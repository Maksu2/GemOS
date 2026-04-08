OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    all PT_LOAD FILEHDR PHDRS FLAGS(7);
}

SECTIONS
{
    . = 0x02000000 + SIZEOF_HEADERS;

    .text ALIGN(4) :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } :all

    .data ALIGN(4) :
    {
        *(.data)
        *(.data.*)
    } :all

    .bss ALIGN(4) (NOLOAD) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :all

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
    }
}
