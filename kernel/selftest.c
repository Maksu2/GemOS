/*
 * Kernel self-test, built only into the self-test image (make selftest
 * compiles the kernel with -DGEMOS_SELFTEST). It runs as a kernel task
 * next to the GUI loop and logs one "[SELFTEST] PASS ..." or
 * "[SELFTEST] FAIL ..." line per check:
 *
 *   - the heap and the small-object pool detect a double free, an
 *     overwritten block header and a write past the end of a block, and
 *     freed heap blocks merge with their free neighbours on both sides;
 *   - malformed ELF images are rejected without leaking memory;
 *   - every exception a Ring 3 program can raise ends only that program
 *     (FAULTS.ELF), while two FPUCHECK.ELF copies and this task keep their
 *     own FPU state;
 *
 * then "[SELFTEST] RESULT: ..." and the kernel stack high-water marks.
 * Last, it overflows its own kernel stack on purpose: the guard page below
 * the stack has to turn that into the double fault panic that names the
 * overflow. tools/smoke.sh --selftest boots the image and checks the log.
 */
#include "selftest.h"

#include "include/heap.h"
#include "memory/kstack.h"
#include "memory/paging.h"
#include "memory/pool.h"
#include "process.h"
#include "scheduler.h"
#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include <gemos/selftest_abi.h>
#include <string.h>

extern uint8_t _binary_faults_elf_start[];
extern uint8_t _binary_faults_elf_end[];
extern uint8_t _binary_fpucheck_elf_start[];
extern uint8_t _binary_fpucheck_elf_end[];

/* More than the self-test starts processes (about 20), so no record is
 * overwritten before the test waiting for it reads it. */
#define SELFTEST_EXIT_SLOTS 64
#define SELFTEST_WAIT_MS 10000U
#define HEAP_HEADER_SIZE 32U /* kernel/heap.c: header right before a block */
#define POOL_HEADER_SIZE 16U /* kernel/memory/pool.c */

typedef struct {
  uint32_t pid;
  int faulted;
  uint32_t vector;
  int32_t exit_code;
} selftest_exit_t;

/* Processes that ended, filled in by the reaper in the GUI task. */
static selftest_exit_t exits[SELFTEST_EXIT_SLOTS];
static uint32_t exit_count;

static uint32_t checks;
static uint32_t failures;
static uint32_t skipped;

/* A test program, patched before it is started */
static uint8_t image[8192];
static size_t image_size;

/* -- reporting ----------------------------------------------------------- */

/* Start a result line; the caller adds details and the newline. */
static int selftest_begin(int ok, const char *what) {
  checks++;
  if (!ok) {
    failures++;
  }
  serial_print(ok ? "[SELFTEST] PASS " : "[SELFTEST] FAIL ");
  serial_print(what);
  return ok;
}

static int selftest_check(int ok, const char *what) {
  selftest_begin(ok, what);
  serial_print("\n");
  return ok;
}

/* -- processes ------------------------------------------------------------ */

static void selftest_sleep(uint32_t ms) {
  scheduler_block_current(timer_get_ticks() + ms);
  scheduler_yield();
}

void selftest_process_exited(const struct process *process) {
  selftest_exit_t *slot = &exits[exit_count % SELFTEST_EXIT_SLOTS];

  slot->pid = process->pid;
  slot->faulted = process->state == PROC_FAULTED;
  slot->vector = process->fault_vector;
  slot->exit_code = process->exit_code;
  exit_count++;
}

static int selftest_wait_exit(int pid, selftest_exit_t *out) {
  uint64_t deadline = timer_get_ticks() + SELFTEST_WAIT_MS;

  for (;;) {
    for (uint32_t i = 0; i < SELFTEST_EXIT_SLOTS && i < exit_count; ++i) {
      if (exits[i].pid == (uint32_t)pid) {
        *out = exits[i];
        return 1;
      }
    }
    if (timer_get_ticks() >= deadline) {
      return 0;
    }
    selftest_sleep(5);
  }
}

/* Copy a test program into image[] and write its test number. */
static int selftest_load(const uint8_t *start, const uint8_t *end,
                         uint32_t test_number) {
  size_t size = (size_t)(end - start);

  image_size = 0;
  if (size > sizeof(image)) {
    return 0;
  }
  memcpy(image, start, size);
  for (size_t i = 0; i + GEMOS_SELFTEST_MARKER_SIZE + 4U <= size; ++i) {
    if (memcmp(image + i, GEMOS_SELFTEST_MARKER,
               GEMOS_SELFTEST_MARKER_SIZE) == 0) {
      memcpy(image + i + GEMOS_SELFTEST_MARKER_SIZE, &test_number,
             sizeof(test_number));
      image_size = size;
      return 1;
    }
  }
  return 0;
}

static int selftest_spawn(const char *name, const uint8_t *start,
                          const uint8_t *end, uint32_t test_number) {
  if (!selftest_load(start, end, test_number)) {
    return -1;
  }
  return process_spawn_user_image(name, image, image_size);
}

/* -- heap and pool ---------------------------------------------------------- */

/* Errors reported so far; leaves the heap in report mode. */
static uint32_t heap_errors(void) { return heap_set_report_mode(1); }

/* Exactly one report since `before`, and about `what`. */
static int heap_reported(uint32_t before, const char *what) {
  const char *last = heap_last_error();

  return heap_errors() == before + 1 && last != NULL && strcmp(last, what) == 0;
}

static int heap_stats_equal(const heap_stats_t *a, const heap_stats_t *b) {
  return a->used_bytes == b->used_bytes && a->free_bytes == b->free_bytes &&
         a->largest_free == b->largest_free &&
         a->used_blocks == b->used_blocks && a->free_blocks == b->free_blocks;
}

static void selftest_heap(void) {
  const uint32_t merge_size = 512U * 1024U;
  /* capacity of a block of merge_size bytes: payload, canary, alignment */
  const uint32_t merge_capacity = (merge_size + 4U + 15U) & ~15U;
  heap_stats_t before, after;
  uint32_t errors;
  uint32_t saved;
  uint8_t *p, *a, *b, *c, *whole;
  int adjacent;

  heap_get_stats(&before);
  errors = heap_errors(); /* report mode from here on */

  p = kalloc(64);
  kfree(p);
  kfree(p);
  selftest_check(heap_reported(errors, "double free"),
                 "heap: double free detected");

  errors = heap_errors();
  p = kalloc(64);
  saved = *(uint32_t *)(p - HEAP_HEADER_SIZE);
  *(uint32_t *)(p - HEAP_HEADER_SIZE) = 0x12345678U;
  kfree(p);
  selftest_check(heap_reported(errors, "kfree with a corrupted block header"),
                 "heap: overwritten block header detected");
  *(uint32_t *)(p - HEAP_HEADER_SIZE) = saved;
  kfree(p);

  errors = heap_errors();
  p = kalloc(10);
  p[10] ^= 0xFFU; /* first byte of the canary behind the block */
  kfree(p);
  selftest_check(heap_reported(errors, "write past the end of a block"),
                 "heap: write past the end of a block detected");
  p[10] ^= 0xFFU;
  kfree(p);

  errors = heap_errors();
  kfree((void *)0x1000);
  selftest_check(heap_reported(errors, "kfree of a pointer outside the heap"),
                 "heap: free of a pointer outside the heap detected");

  errors = heap_errors();
  p = kalloc(256);
  memset(p, 0, 256);
  kfree(p + 64);
  selftest_check(heap_reported(errors, "kfree with a corrupted block header"),
                 "heap: free of a pointer into a block detected");
  kfree(p);

  /* Three neighbours from the big free block at the end of the heap. Freed
   * as a, c, b, they merge into one block only if b merges with both
   * neighbours: then a request for all of it gets exactly a back. */
  errors = heap_errors();
  a = kalloc(merge_size);
  b = kalloc(merge_size);
  c = kalloc(merge_size);
  adjacent = a != NULL && b == a + merge_capacity + HEAP_HEADER_SIZE &&
             c == b + merge_capacity + HEAP_HEADER_SIZE;
  kfree(a);
  kfree(c);
  kfree(b);
  whole = kalloc(3U * merge_capacity + 2U * HEAP_HEADER_SIZE - 4U);
  selftest_begin(adjacent && whole == a && heap_errors() == errors,
                 "heap: a freed block merges with free neighbours on both "
                 "sides");
  serial_print(adjacent ? "\n" : " (the three blocks were not adjacent)\n");
  kfree(whole);

  heap_get_stats(&after);
  heap_set_report_mode(0);
  selftest_check(heap_stats_equal(&before, &after),
                 "heap: statistics back to the start, nothing leaked");
}

static void selftest_pool(void) {
  static small_pool_t pool;
  const size_t size = 64U * 1024U;
  uint8_t *memory = kalloc(size);
  uint32_t errors = heap_errors();
  uint32_t saved;
  uint8_t *p, *q;

  pool_init(&pool, memory, size);
  p = pool_alloc(&pool, 24);
  pool_free(&pool, p);
  pool_free(&pool, p);
  selftest_check(heap_reported(errors, "double free in the small-object pool"),
                 "pool: double free detected");

  q = pool_alloc(&pool, 24);
  selftest_check(q == p, "pool: a freed block is handed out again");

  errors = heap_errors();
  saved = *(uint32_t *)(q - POOL_HEADER_SIZE);
  *(uint32_t *)(q - POOL_HEADER_SIZE) = 0;
  pool_free(&pool, q);
  selftest_check(
      heap_reported(errors, "free with a corrupted pool block header"),
      "pool: overwritten block header detected");
  *(uint32_t *)(q - POOL_HEADER_SIZE) = saved;
  pool_free(&pool, q);
  selftest_check(pool_in_use(&pool) == 0, "pool: every block back");

  heap_set_report_mode(0);
  kfree(memory);
}

/* -- ELF loader --------------------------------------------------------------- */

#define ELF_TYPE 16
#define ELF_ENTRY 24
#define ELF_PHOFF 28
#define ELF_PHENTSIZE 42
#define ELF_PHNUM 44
#define PHDR_SIZE 32
#define PHDR_OFFSET 4
#define PHDR_VADDR 8
#define PHDR_FILESZ 16
#define PHDR_MEMSZ 20

static uint32_t get32(const uint8_t *p) {
  uint32_t value;
  memcpy(&value, p, sizeof(value));
  return value;
}

static void put32(uint8_t *p, uint32_t value) { memcpy(p, &value, sizeof(value)); }

static void put16(uint8_t *p, uint16_t value) {
  memcpy(p, &value, sizeof(value));
}

static const char *const elf_cases[] = {
    "a file shorter than the ELF header",
    "a bad magic number",
    "a shared object (ET_DYN)",
    "program headers past the end (e_phoff + size wraps around)",
    "a wrong program header size",
    "too many program headers",
    "segment data past the end (p_offset + p_filesz wraps around)",
    "p_filesz larger than p_memsz",
    "a segment whose end wraps around",
    "a segment in kernel memory",
    "a segment over the user stack",
    "overlapping segments",
    "segments that share a page",
    "an entry point outside the segments",
};

static void selftest_elf_break(unsigned int index) {
  uint8_t *ph0 = image + get32(image + ELF_PHOFF);
  uint8_t *ph1 = ph0 + PHDR_SIZE;
  uint32_t text_end = get32(ph0 + PHDR_VADDR) + get32(ph0 + PHDR_MEMSZ);

  switch (index) {
  case 0: image_size = 40; break;
  case 1: image[1] = 'X'; break;
  case 2: put16(image + ELF_TYPE, 3); break;
  case 3: put32(image + ELF_PHOFF, 0xFFFFFFE0U); break;
  case 4: put16(image + ELF_PHENTSIZE, 16); break;
  case 5: put16(image + ELF_PHNUM, 0x100); break;
  case 6:
    put32(ph0 + PHDR_OFFSET, 0xFFFFFF00U);
    put32(ph0 + PHDR_FILESZ, 0x200U);
    put32(ph0 + PHDR_MEMSZ, 0x200U);
    break;
  case 7: put32(ph0 + PHDR_FILESZ, get32(ph0 + PHDR_MEMSZ) + 1U); break;
  case 8: put32(ph0 + PHDR_MEMSZ, 0xFFFFF000U); break;
  case 9: put32(ph0 + PHDR_VADDR, 0x00100000U); break;
  case 10: put32(ph1 + PHDR_VADDR, PAGING_USER_STACK_TOP - PAGE_SIZE); break;
  case 11: put32(ph1 + PHDR_VADDR, get32(ph0 + PHDR_VADDR)); break;
  case 12:
    put32(ph1 + PHDR_VADDR, (text_end & (PAGE_SIZE - 1U)) != 0 ? text_end
                                                              : text_end - 16U);
    break;
  case 13: put32(image + ELF_ENTRY, 0x03000000U); break;
  }
}

static void selftest_elf(void) {
  heap_stats_t before, after;
  uint32_t frames = page_frames_free();
  selftest_exit_t exit;
  int pid;

  selftest_load(_binary_fpucheck_elf_start, _binary_fpucheck_elf_end, 0);
  if (!selftest_check(image_size != 0 && image[ELF_PHNUM] == 2,
                      "elf: FPUCHECK.ELF has two program headers")) {
    return;
  }

  heap_get_stats(&before);
  for (unsigned int i = 0; i < sizeof(elf_cases) / sizeof(elf_cases[0]); ++i) {
    selftest_load(_binary_fpucheck_elf_start, _binary_fpucheck_elf_end, 0);
    selftest_elf_break(i);
    pid = process_spawn_user_image("BROKEN.ELF", image, image_size);
    selftest_begin(pid < 0, "elf: rejects ");
    serial_print(elf_cases[i]);
    serial_print("\n");
    if (pid >= 0) {
      process_kill_pid((uint32_t)pid, -1);
    }
  }
  heap_get_stats(&after);
  selftest_check(heap_stats_equal(&before, &after) &&
                     page_frames_free() == frames,
                 "elf: rejected images leak no heap memory or page frames");

  pid = selftest_spawn("FPUCHECK.ELF", _binary_fpucheck_elf_start,
                       _binary_fpucheck_elf_end, 0);
  selftest_check(pid >= 0 && selftest_wait_exit(pid, &exit) && !exit.faulted &&
                     exit.exit_code == GEMOS_FPUCHECK_OK,
                 "elf: the unmodified image loads and runs");
}

/* -- exceptions in Ring 3 ---------------------------------------------------- */

typedef struct {
  uint32_t number;
  uint32_t vector;
  const char *name;
} selftest_fault_t;

static const selftest_fault_t faults[] = {
    {GEMOS_FAULT_DIVIDE, 0, "#DE div by zero"},
    {GEMOS_FAULT_DEBUG, 1, "#DB single step"},
    {GEMOS_FAULT_BREAKPOINT, 3, "#BP int3"},
    {GEMOS_FAULT_OVERFLOW, 4, "#OF into"},
    {GEMOS_FAULT_BOUND, 5, "#BR bound"},
    {GEMOS_FAULT_INVALID_OPCODE, 6, "#UD ud2"},
    {GEMOS_FAULT_INVALID_TSS, 10, "#TS iret with EFLAGS.NT"},
    {GEMOS_FAULT_NOT_PRESENT, 11, "#NP DS loaded with a not-present segment"},
    {GEMOS_FAULT_STACK_SEGMENT, 12, "#SS SS loaded with a not-present segment"},
    {GEMOS_FAULT_GP_PRIVILEGED, 13, "#GP cli"},
    {GEMOS_FAULT_GP_KERNEL_GATE, 13, "#GP int 0x81 (kernel-only gate)"},
    {GEMOS_FAULT_PF_CODE_WRITE, 14, "#PF write to the program's code"},
    {GEMOS_FAULT_PF_KERNEL_READ, 14, "#PF read of kernel memory"},
    {GEMOS_FAULT_PF_KERNEL_JUMP, 14, "#PF call into kernel code"},
    {GEMOS_FAULT_PF_STACK, 14, "#PF user stack overflow"},
    {GEMOS_FAULT_X87, 16, "#MF unmasked x87 divide by zero"},
    {GEMOS_FAULT_SIMD, 19, "#XM unmasked SSE divide by zero"},
};

_Static_assert(sizeof(faults) / sizeof(faults[0]) == GEMOS_FAULT_COUNT,
               "one test per FAULTS.ELF test number");

static void selftest_faults(void) {
  for (uint32_t i = 0; i < GEMOS_FAULT_COUNT; ++i) {
    const selftest_fault_t *fault = &faults[i];
    selftest_exit_t exit;
    int pid = selftest_spawn("FAULTS.ELF", _binary_faults_elf_start,
                             _binary_faults_elf_end, fault->number);
    int ended = pid >= 0 && selftest_wait_exit(pid, &exit);

    if (ended && !exit.faulted &&
        exit.exit_code == GEMOS_SELFTEST_NOT_RAISED) {
      /* not a check: the CPU cannot raise it, so the kernel never sees it */
      skipped++;
      serial_print("[SELFTEST] SKIP fault ");
      serial_print(fault->name);
      serial_print(": this CPU flags it but does not raise it\n");
      continue;
    }
    selftest_begin(ended && exit.faulted && exit.vector == fault->vector,
                   "fault ");
    serial_print(fault->name);
    if (pid < 0) {
      serial_print(": the program did not start\n");
      continue;
    }
    serial_print(": PID ");
    serial_print_dec((uint32_t)pid);
    if (!ended) {
      serial_print(" still running after 10 s\n");
      process_kill_pid((uint32_t)pid, -1);
    } else if (exit.faulted) {
      serial_print(" ended by vector ");
      serial_print_dec(exit.vector);
      serial_print("\n");
    } else {
      serial_print(" exited with code ");
      serial_print_dec((uint32_t)exit.exit_code);
      serial_print(", no exception\n");
    }
  }
}

/* -- FPU state -------------------------------------------------------------- */

/* This task loads pi with round-toward-zero and gives up the CPU many
 * times while the FPUCHECK copies and the GUI use the FPU. */
static int selftest_fpu_kernel_task(void) {
  static const uint16_t control_word = 0x0F7F;
  uint8_t before[10], after[10];
  uint16_t control_after;
  uint32_t rounds = 200;

  __asm__ volatile("fninit\n\t"
                   "fldcw %[cw]\n\t"
                   "fldpi\n\t"
                   "fld %%st(0)\n\t"
                   "fstpt %[before]\n\t"
                   "1:\n\t"
                   "int $0x81\n\t" /* scheduler_yield() */
                   "dec %[rounds]\n\t"
                   "jnz 1b\n\t"
                   "fstpt %[after]\n\t"
                   "fnstcw %[cw_after]\n\t"
                   "fninit\n\t"
                   : [before] "=m"(before), [after] "=m"(after),
                     [cw_after] "=m"(control_after), [rounds] "+r"(rounds)
                   : [cw] "m"(control_word)
                   : "memory", "cc");
  return memcmp(before, after, sizeof(before)) == 0 &&
         control_after == control_word;
}

static void selftest_fpu_result(int pid, const char *what) {
  selftest_exit_t exit;
  int ended = pid >= 0 && selftest_wait_exit(pid, &exit);

  selftest_begin(ended && !exit.faulted && exit.exit_code == GEMOS_FPUCHECK_OK,
                 what);
  if (!ended) {
    serial_print(": did not end\n");
  } else if (exit.faulted) {
    serial_print(": faulted with vector ");
    serial_print_dec(exit.vector);
    serial_print("\n");
  } else if (exit.exit_code == GEMOS_FPUCHECK_X87_CHANGED) {
    serial_print(": x87 registers changed\n");
  } else if (exit.exit_code == GEMOS_FPUCHECK_SSE_CHANGED) {
    serial_print(": SSE registers changed\n");
  } else {
    serial_print("\n");
  }
}

/* -- kernel stack overflow -------------------------------------------------- */

static __attribute__((noinline)) uint32_t
selftest_recurse(volatile uint32_t *depth) {
  volatile uint8_t frame[256];

  frame[0] = (uint8_t)*depth;
  *depth += 1;
  if (*depth > 1000000U) { /* never: the stack is 16 KB */
    return frame[0];
  }
  return selftest_recurse(depth) + frame[0];
}

/* -- the task --------------------------------------------------------------- */

static void selftest_main(void) {
  volatile uint32_t depth = 0;
  int fpu_a, fpu_b;

  serial_print("[SELFTEST] Start\n");
  selftest_heap();
  selftest_pool();
  selftest_elf();

  fpu_a = selftest_spawn("FPUCHECK.ELF", _binary_fpucheck_elf_start,
                         _binary_fpucheck_elf_end, 1);
  fpu_b = selftest_spawn("FPUCHECK.ELF", _binary_fpucheck_elf_start,
                         _binary_fpucheck_elf_end, 2);
  selftest_check(selftest_fpu_kernel_task(),
                 "fpu: a kernel task keeps its x87 registers across 200 "
                 "task switches");
  selftest_faults();
  selftest_fpu_result(fpu_a, "fpu: FPUCHECK.ELF set 1 kept its x87 and SSE "
                             "registers for 3 s, through all faults");
  selftest_fpu_result(fpu_b, "fpu: FPUCHECK.ELF set 2 kept its x87 and SSE "
                             "registers for 3 s, through all faults");

  kstack_log_high_water();
  serial_print(failures == 0 ? "[SELFTEST] RESULT: PASS (" :
                               "[SELFTEST] RESULT: FAIL (");
  if (failures != 0) {
    serial_print_dec(failures);
    serial_print(" of ");
  }
  serial_print_dec(checks);
  serial_print(" checks");
  if (skipped != 0) {
    serial_print(", ");
    serial_print_dec(skipped);
    serial_print(" skipped");
  }
  serial_print(")\n");

  selftest_sleep(1000);
  serial_print("[SELFTEST] Overflowing the kernel stack on purpose: expect a "
               "double fault\n");
  selftest_recurse(&depth);
  serial_print("[SELFTEST] FAIL the kernel stack overflow went unnoticed\n");
  for (;;) {
    selftest_sleep(1000);
  }
}

void selftest_start(void) {
  int slot = kstack_alloc();

  if (slot < 0 || task_create_kernel(selftest_main, kstack_top(slot)) < 0) {
    serial_print("[SELFTEST] FAIL cannot start the self-test task\n");
  }
}
