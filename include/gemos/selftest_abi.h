#ifndef GEMOS_SELFTEST_ABI_H
#define GEMOS_SELFTEST_ABI_H

/*
 * Self-test image only (make selftest): kernel/selftest.c and the test
 * programs in userland/selftest/ agree on these values. Macros only, so
 * the assembly programs can include this file too.
 *
 * A test program's image contains GEMOS_SELFTEST_MARKER followed by a
 * 32-bit test number; the kernel writes the number before it starts the
 * program.
 */
#define GEMOS_SELFTEST_MARKER "GEMOSTESTARG"
#define GEMOS_SELFTEST_MARKER_SIZE 12

/* Ring 3 data segment that is marked not present (kernel/gdt.c, self-test
 * builds): loading it into DS raises #NP, into SS #SS. */
#define GEMOS_SELFTEST_NP_SELECTOR 0x3B

/* FAULTS.ELF: raises one exception per test number. It exits with
 * GEMOS_SELFTEST_SURVIVED if the exception did not come. */
#define GEMOS_FAULT_DIVIDE 0          /* #DE div by zero */
#define GEMOS_FAULT_DEBUG 1           /* #DB single step (EFLAGS.TF) */
#define GEMOS_FAULT_BREAKPOINT 2      /* #BP int3 */
#define GEMOS_FAULT_OVERFLOW 3        /* #OF into */
#define GEMOS_FAULT_BOUND 4           /* #BR bound */
#define GEMOS_FAULT_INVALID_OPCODE 5  /* #UD ud2 */
#define GEMOS_FAULT_INVALID_TSS 6     /* #TS iret with EFLAGS.NT */
#define GEMOS_FAULT_NOT_PRESENT 7     /* #NP DS <- not-present segment */
#define GEMOS_FAULT_STACK_SEGMENT 8   /* #SS SS <- not-present segment */
#define GEMOS_FAULT_GP_PRIVILEGED 9   /* #GP cli */
#define GEMOS_FAULT_GP_KERNEL_GATE 10 /* #GP int 0x81 (kernel-only gate) */
#define GEMOS_FAULT_PF_CODE_WRITE 11  /* #PF write to the program's code */
#define GEMOS_FAULT_PF_KERNEL_READ 12 /* #PF read kernel memory */
#define GEMOS_FAULT_PF_KERNEL_JUMP 13 /* #PF call into kernel code */
#define GEMOS_FAULT_PF_STACK 14       /* #PF run off the user stack */
#define GEMOS_FAULT_X87 15            /* #MF unmasked x87 divide by zero */
#define GEMOS_FAULT_SIMD 16           /* #XM unmasked SSE divide by zero */
#define GEMOS_FAULT_COUNT 17
#define GEMOS_SELFTEST_SURVIVED 99
/* The CPU flagged the exception but did not raise it (QEMU's TCG does not
 * raise #XM, for one). */
#define GEMOS_SELFTEST_NOT_RAISED 98

/* FPUCHECK.ELF: test number 1 or 2 selects one of two sets of x87 and SSE
 * register values, which the program keeps checking for
 * GEMOS_FPUCHECK_MS milliseconds while it gives up the CPU; 0 exits at
 * once. Exit codes: */
#define GEMOS_FPUCHECK_OK 0
#define GEMOS_FPUCHECK_X87_CHANGED 1
#define GEMOS_FPUCHECK_SSE_CHANGED 2
#define GEMOS_FPUCHECK_BAD_ARGUMENT 3
#define GEMOS_FPUCHECK_MS 3000

/* FILETEST.ELF: the file syscalls from Ring 3. Its image holds a table of
 * GEMOS_FILETEST_SIZE bytes (byte i is GEMOS_FILETEST_BYTE(i)), so it is
 * larger than 64 KB and also tests the loader. It checks the table, writes
 * it to GEMOS_FILETEST_BIG and reads it back, writes a small file in
 * GEMOS_FILETEST_SUBDIR (the kernel creates the directories), then tries
 * to change programs. Exit code: 0, or the first step that failed. */
#define GEMOS_FILETEST_SIZE 81920
#define GEMOS_FILETEST_BYTE(i) (((((i) >> 8) ^ (((i) * 13) + 5))) & 0xFF)
#define GEMOS_FILETEST_BIG "/fstest/big.bin"
#define GEMOS_FILETEST_SUBDIR "/fstest/sub/deeper"
#define GEMOS_FILETEST_NOTE "/fstest/sub/deeper/note.txt"
#define GEMOS_FILETEST_NOTE_TEXT "Written by FILETEST.ELF through SYS_file_write.\n"
#define GEMOS_FILETEST_OK 0
#define GEMOS_FILETEST_IMAGE 1        /* the table in the image is wrong */
#define GEMOS_FILETEST_WRITE 2        /* writing GEMOS_FILETEST_BIG failed */
#define GEMOS_FILETEST_READ 3         /* reading it back failed or differed */
#define GEMOS_FILETEST_NOTE_FAILED 4  /* GEMOS_FILETEST_NOTE failed */
#define GEMOS_FILETEST_READ_PROGRAM 5 /* reading UTERM.ELF failed */
#define GEMOS_FILETEST_OVERWRITE 6    /* overwriting UTERM.ELF was allowed */
#define GEMOS_FILETEST_OTHER_CASE 7   /* creating uterm.elf was allowed */
#define GEMOS_FILETEST_SLASH 8        /* writing "UTERM.ELF/" was allowed */
#define GEMOS_FILETEST_NEW_PROGRAM 9  /* creating /fstest/NEW.ELF was allowed */
#define GEMOS_FILETEST_NO_DIR 10      /* a write into a missing directory did
                                         not return GEMOS_ERR_NOENT */

#endif /* GEMOS_SELFTEST_ABI_H */
