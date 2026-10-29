/*
 * The table of FILETEST.ELF (self-test image only): GEMOS_FILETEST_SIZE
 * bytes, byte i = GEMOS_FILETEST_BYTE(i). It makes the program larger than
 * 64 KB; filetest.c checks every byte, so a loader that copied only part of
 * the image, or put a page in the wrong place, fails the test.
 */
#include <gemos/selftest_abi.h>

.section .rodata
.global filetest_table
filetest_table:
.set filetest_index, 0
.rept GEMOS_FILETEST_SIZE
    .byte GEMOS_FILETEST_BYTE(filetest_index)
    .set filetest_index, filetest_index + 1
.endr
