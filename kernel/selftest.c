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
 *   - GemFS: directories, a 100 KB file, replacing and deleting, errors, a
 *     write that fails part way, many entries in one directory; the ATA
 *     driver refuses writes to a disk GemFS did not mount; FILETEST.ELF
 *     (over 64 KB, started from GemFS) uses the file syscalls and may not
 *     change programs; files written on the first boot of a disk are
 *     checked on the next one (the harness boots twice);
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

#include "fs/gemfs.h"
#include "include/heap.h"
#include "memory/kstack.h"
#include "memory/paging.h"
#include "memory/pool.h"
#include "process.h"
#include "scheduler.h"
#include "../drivers/ata.h"
#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include <gemos/selftest_abi.h>
#include <string.h>

extern uint8_t _binary_faults_elf_start[];
extern uint8_t _binary_faults_elf_end[];
extern uint8_t _binary_fpucheck_elf_start[];
extern uint8_t _binary_fpucheck_elf_end[];
extern uint8_t _binary_filetest_elf_start[];
extern uint8_t _binary_filetest_elf_end[];
extern uint8_t _binary_uterm_image_bin_start[];
extern uint8_t _binary_uterm_image_bin_end[];

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

/* -- GemFS --------------------------------------------------------------------- */

#define FS_BIG_SIZE (100U * 1024U) /* 25 blocks: 12 direct, 13 indirect */
#define FS_MANY 70U                /* two directory blocks of 64 entries */
#define PERSIST_NOTE "/persist/a/b/note.txt"
#define PERSIST_TEXT "Written by the GemOS self-test; the next boot reads it.\n"

static gemfs_entry_t listed_entry;

static void selftest_keep_entry(void *ctx, const gemfs_entry_t *entry) {
  (void)ctx;
  listed_entry = *entry;
}

static int selftest_memory_source(void *ctx, uint32_t offset, void *dst,
                                  uint32_t len) {
  memcpy(dst, (const uint8_t *)ctx + offset, len);
  return 0;
}

/* Gives up on the third block of a write. */
static int selftest_failing_source(void *ctx, uint32_t offset, void *dst,
                                   uint32_t len) {
  (void)ctx;
  memset(dst, 0xEE, len);
  return offset >= 2U * GEMFS_BLOCK_SIZE;
}

/* Every directory of path, like mkdir -p. */
static int selftest_mkdirs(const char *path) {
  char partial[GEMFS_PATH_MAX];
  size_t length = strlen(path);

  if (length >= sizeof(partial)) {
    return 0;
  }
  for (size_t i = 1; i <= length; ++i) {
    if (path[i] == '/' || path[i] == '\0') {
      int result;

      memcpy(partial, path, i);
      partial[i] = '\0';
      result = gemfs_mkdir(partial);
      if (result != GEMFS_OK && result != GEMFS_ERR_EXIST) {
        return 0;
      }
    }
  }
  return 1;
}

/* The file is exactly size bytes of data (NULL: the FILETEST table). */
static int selftest_file_is(const char *path, const uint8_t *data,
                            uint32_t size) {
  static uint8_t chunk[GEMFS_BLOCK_SIZE];
  gemfs_stat_t stat;

  if (gemfs_stat(path, &stat) != GEMFS_OK || stat.type != GEMFS_TYPE_FILE ||
      stat.size != size) {
    return 0;
  }
  for (uint32_t offset = 0; offset < size; offset += GEMFS_BLOCK_SIZE) {
    uint32_t length = size - offset < GEMFS_BLOCK_SIZE ? size - offset
                                                        : GEMFS_BLOCK_SIZE;

    if (gemfs_read(stat.inode, offset, chunk, length) != (int)length) {
      return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
      uint32_t at = offset + i;

      if (chunk[i] != (data != NULL ? data[at]
                                    : (uint8_t)GEMOS_FILETEST_BYTE(at))) {
        return 0;
      }
    }
  }
  return 1;
}

/* The first boot of a disk writes a file; the harness boots it again and
 * this checks that file and the ones FILETEST.ELF wrote. */
static void selftest_fs_persist(void) {
  static const char text[] = PERSIST_TEXT;
  static const char note[] = GEMOS_FILETEST_NOTE_TEXT;
  gemfs_stat_t stat;

  if (gemfs_stat(PERSIST_NOTE, &stat) == GEMFS_OK) {
    selftest_check(selftest_file_is(PERSIST_NOTE, (const uint8_t *)text,
                                    sizeof(text) - 1U),
                   "fs: after a reboot, " PERSIST_NOTE " (from the kernel) "
                   "is intact");
    selftest_check(selftest_file_is(GEMOS_FILETEST_BIG, NULL,
                                    GEMOS_FILETEST_SIZE) &&
                       selftest_file_is(GEMOS_FILETEST_NOTE,
                                        (const uint8_t *)note,
                                        sizeof(note) - 1U),
                   "fs: after a reboot, the files FILETEST.ELF wrote are "
                   "intact");
    return;
  }
  selftest_check(selftest_mkdirs("/persist/a/b") &&
                     gemfs_write(PERSIST_NOTE, text, sizeof(text) - 1U, 0,
                                 0) == (int)(sizeof(text) - 1U),
                 "fs: wrote " PERSIST_NOTE " for the next boot to check");
}

static int selftest_fs_names(uint8_t *big, uint8_t *back) {
  char path[GEMFS_PATH_MAX + 8];
  gemfs_stat_t stat;

  /* a 56-byte name, and a path of GEMFS_PATH_MAX bytes */
  strcpy(path, "/selftest/");
  memset(path + 10, 'n', 56);
  path[66] = '\0';
  if (gemfs_write(path, big, 10, 0, 0) != GEMFS_ERR_NAME) {
    return 0;
  }
  memset(path, 'p', GEMFS_PATH_MAX);
  path[0] = '/';
  path[GEMFS_PATH_MAX] = '\0';
  if (gemfs_stat(path, &stat) != GEMFS_ERR_NAME) {
    return 0;
  }
  return gemfs_write("/selftest/a", big, 10, 0, 0) == GEMFS_ERR_ISDIR &&
         gemfs_stat("/selftest/a", &stat) == GEMFS_OK &&
         gemfs_read(stat.inode, 0, back, 10) == GEMFS_ERR_ISDIR &&
         gemfs_stat("/selftest/none", &stat) == GEMFS_ERR_NOENT &&
         gemfs_write("/selftest/a/b/big.bin/x", big, 10, 0, 0) ==
             GEMFS_ERR_NOTDIR &&
         gemfs_stat("/selftest/../selftest", &stat) == GEMFS_ERR_NAME &&
         gemfs_write("/selftest//x", big, 10, 0, 0) == GEMFS_ERR_NAME &&
         gemfs_write("/selftest/huge", big, GEMFS_MAX_FILE_SIZE + 1U, 0, 0) ==
             GEMFS_ERR_TOOBIG &&
         gemfs_delete("/") == GEMFS_ERR_NAME;
}

/* FS_MANY files, three deleted and two written again into the gaps: the
 * others must all still be there. */
static int selftest_fs_many(void) {
  char path[32];
  uint32_t value;
  gemfs_stat_t stat;
  int ok = gemfs_mkdir("/selftest/many") == GEMFS_OK;

  for (uint32_t i = 0; i < FS_MANY && ok; ++i) {
    strcpy(path, "/selftest/many/f00");
    path[16] = (char)('0' + i / 10U);
    path[17] = (char)('0' + i % 10U);
    ok = gemfs_write(path, &i, sizeof(i), 0, 0) == (int)sizeof(i);
  }
  ok = ok && gemfs_list("/selftest/many", NULL, NULL) == (int)FS_MANY &&
       gemfs_delete("/selftest/many/f03") == GEMFS_OK &&
       gemfs_delete("/selftest/many/f63") == GEMFS_OK &&
       gemfs_delete("/selftest/many/f64") == GEMFS_OK &&
       gemfs_list("/selftest/many", NULL, NULL) == (int)FS_MANY - 3 &&
       gemfs_write("/selftest/many/f03", "\x03\0\0\0", 4, 0, 0) == 4 &&
       gemfs_write("/selftest/many/f63", "\x3F\0\0\0", 4, 0, 0) == 4;
  for (uint32_t i = 0; i < FS_MANY && ok; ++i) {
    strcpy(path, "/selftest/many/f00");
    path[16] = (char)('0' + i / 10U);
    path[17] = (char)('0' + i % 10U);
    if (i == 64U) {
      ok = gemfs_stat(path, &stat) == GEMFS_ERR_NOENT;
      continue;
    }
    ok = gemfs_stat(path, &stat) == GEMFS_OK &&
         gemfs_read(stat.inode, 0, &value, sizeof(value)) ==
             (int)sizeof(value) &&
         value == i;
  }
  return ok && gemfs_list("/selftest/many", NULL, NULL) == (int)FS_MANY - 1;
}

static void selftest_fs_api(void) {
  gemfs_usage_t before, with_dirs, after, now;
  gemfs_stat_t stat;
  uint8_t *big = (uint8_t *)kalloc(FS_BIG_SIZE);
  uint8_t *back = (uint8_t *)kalloc(FS_BIG_SIZE);
  int ok;

  if (!selftest_check(big != NULL && back != NULL,
                      "fs: memory for the file tests")) {
    kfree(big);
    kfree(back);
    return;
  }
  for (uint32_t i = 0; i < FS_BIG_SIZE; ++i) {
    big[i] = (uint8_t)(i * 7U + (i >> 12));
  }
  (void)gemfs_delete("/selftest"); /* left over from a failed run */
  (void)gemfs_usage(&before);

  ok = gemfs_mkdir("/selftest") == GEMFS_OK &&
       gemfs_mkdir("/selftest/a") == GEMFS_OK &&
       gemfs_mkdir("/selftest/a/b") == GEMFS_OK &&
       gemfs_stat("/selftest/a/b", &stat) == GEMFS_OK &&
       stat.type == GEMFS_TYPE_DIR;
  selftest_check(ok, "fs: mkdir /selftest, /selftest/a, /selftest/a/b");
  selftest_check(gemfs_mkdir("/selftest/a") == GEMFS_ERR_EXIST &&
                     gemfs_mkdir("/selftest/x/y") == GEMFS_ERR_NOENT,
                 "fs: mkdir of a taken name or in a missing directory fails");
  (void)gemfs_usage(&with_dirs);

  ok = gemfs_write("/selftest/a/b/big.bin", big, FS_BIG_SIZE, 0, 0) ==
           (int)FS_BIG_SIZE &&
       gemfs_stat("/selftest/a/b/big.bin", &stat) == GEMFS_OK &&
       stat.size == FS_BIG_SIZE &&
       gemfs_read(stat.inode, 0, back, FS_BIG_SIZE) == (int)FS_BIG_SIZE &&
       memcmp(big, back, FS_BIG_SIZE) == 0;
  /* reads across a block, across the last direct block, past the end */
  ok = ok && gemfs_read(stat.inode, 4090U, back, 20) == 20 &&
       memcmp(big + 4090U, back, 20) == 0 &&
       gemfs_read(stat.inode, 12U * GEMFS_BLOCK_SIZE - 5U, back, 10) == 10 &&
       memcmp(big + 12U * GEMFS_BLOCK_SIZE - 5U, back, 10) == 0 &&
       gemfs_read(stat.inode, FS_BIG_SIZE - 3U, back, 10) == 3 &&
       gemfs_read(stat.inode, FS_BIG_SIZE, back, 10) == 0;
  (void)gemfs_usage(&after);
  /* 25 data blocks, the indirect block, and the first block of the empty
   * directory /selftest/a/b */
  selftest_check(ok && with_dirs.free_blocks - after.free_blocks == 27U &&
                     with_dirs.free_inodes - after.free_inodes == 1U,
                 "fs: a 100 KB file in /selftest/a/b reads back and takes 25 "
                 "data blocks, the indirect one and a directory block");

  ok = gemfs_write("/selftest/a/b/big.bin", big + 1, 5000, 0, 0) == 5000 &&
       gemfs_stat("/selftest/a/b/big.bin", &stat) == GEMFS_OK &&
       stat.size == 5000 &&
       gemfs_read(stat.inode, 0, back, FS_BIG_SIZE) == 5000 &&
       memcmp(big + 1, back, 5000) == 0;
  (void)gemfs_usage(&after);
  selftest_check(ok && with_dirs.free_blocks - after.free_blocks == 3U &&
                     with_dirs.free_inodes - after.free_inodes == 1U,
                 "fs: replacing it with 5000 bytes frees the old blocks");

  ok = gemfs_write_from("/selftest/a/b/big.bin", 10000,
                        selftest_failing_source, NULL, 0, 0) ==
           GEMFS_ERR_SOURCE &&
       gemfs_stat("/selftest/a/b/big.bin", &stat) == GEMFS_OK &&
       stat.size == 5000 &&
       gemfs_read(stat.inode, 0, back, FS_BIG_SIZE) == 5000 &&
       memcmp(big + 1, back, 5000) == 0;
  (void)gemfs_usage(&now);
  selftest_check(ok && now.free_blocks == after.free_blocks &&
                     now.free_inodes == after.free_inodes,
                 "fs: a write that fails part way keeps the old contents "
                 "and gives its blocks back");

  ok = gemfs_list("/selftest/a", selftest_keep_entry, NULL) == 1 &&
       strcmp(listed_entry.name, "b") == 0 &&
       listed_entry.type == GEMFS_TYPE_DIR &&
       gemfs_list("/selftest/a/b", selftest_keep_entry, NULL) == 1 &&
       strcmp(listed_entry.name, "big.bin") == 0 &&
       listed_entry.type == GEMFS_TYPE_FILE;
  selftest_check(ok, "fs: listing /selftest/a and /selftest/a/b");

  selftest_check(selftest_fs_names(big, back),
                 "fs: errors for a directory, a missing file, a file used as "
                 "a directory, bad names, a file over 4 MB, the root");
  selftest_check(selftest_fs_many(),
                 "fs: 70 files in one directory (two blocks); with three "
                 "deleted and two written again all others are found");

  ok = gemfs_write_user("/selftest/X.ELF", 10, selftest_memory_source, big) ==
           GEMFS_ERR_DENIED &&
       gemfs_write_user("/selftest/y.eLf", 10, selftest_memory_source, big) ==
           GEMFS_ERR_DENIED &&
       gemfs_write("/selftest/system.txt", big, 10, GEMFS_FLAG_SYSTEM, 1) ==
           10 &&
       gemfs_write_user("/selftest/system.txt", 10, selftest_memory_source,
                        big + 1) == GEMFS_ERR_DENIED &&
       gemfs_write_user("/selftest/system.txt/", 10, selftest_memory_source,
                        big + 1) == GEMFS_ERR_DENIED &&
       selftest_file_is("/selftest/system.txt", big, 10) &&
       gemfs_stat("/selftest/X.ELF", &stat) == GEMFS_ERR_NOENT &&
       gemfs_write_user("/selftest/user.txt", 10, selftest_memory_source,
                        big) == 10;
  selftest_check(ok, "fs: process writes to *.ELF names and system files "
                     "are refused, others go through");

  ok = gemfs_delete("/selftest") == GEMFS_OK &&
       gemfs_stat("/selftest", &stat) == GEMFS_ERR_NOENT &&
       gemfs_stat("/selftest/a/b/big.bin", &stat) == GEMFS_ERR_NOENT;
  (void)gemfs_usage(&after);
  selftest_check(ok && after.free_blocks == before.free_blocks &&
                     after.free_inodes == before.free_inodes,
                 "fs: deleting /selftest removes the whole tree and frees "
                 "every block and inode");
  kfree(big);
  kfree(back);
}

/* Disks other than the mounted one are read-only. The harness attaches one
 * without GemFS; writing its first sector back unchanged must fail. */
static void selftest_fs_write_gate(void) {
  static uint8_t sector[512];
  int tested = 0;
  int refused = 1;

  for (int device = 0; device < ATA_MAX_DEVICES; ++device) {
    if (ata_get_device(device) == NULL ||
        ata_read(device, 0, 1, sector) != ATA_OK ||
        memcmp(sector, "GEMOS-FS", 8) == 0) {
      continue; /* absent, unreadable or the GemFS disk */
    }
    tested++;
    if (ata_write(device, 0, 1, sector) != ATA_ERR_READ_ONLY) {
      refused = 0;
    }
  }
  selftest_check(tested > 0 && refused,
                 "fs: the ATA driver refuses writes to a disk GemFS did not "
                 "mount");
}

static const char *const filetest_steps[] = {
    "passed every step",
    "found its table damaged",
    "could not write " GEMOS_FILETEST_BIG,
    "read " GEMOS_FILETEST_BIG " back wrong",
    "failed on " GEMOS_FILETEST_NOTE,
    "could not read UTERM.ELF",
    "was allowed to overwrite UTERM.ELF",
    "was allowed to create uterm.elf",
    "was allowed to write \"UTERM.ELF/\"",
    "was allowed to create /fstest/NEW.ELF",
    "got no GEMOS_ERR_NOENT for a missing directory",
};

static void selftest_fs_filetest(void) {
  uint32_t size =
      (uint32_t)(_binary_filetest_elf_end - _binary_filetest_elf_start);
  uint32_t uterm_size = (uint32_t)(_binary_uterm_image_bin_end -
                                   _binary_uterm_image_bin_start);
  gemfs_stat_t stat;
  selftest_exit_t exit;
  int ended;
  int pid;

  selftest_check(size > 64U * 1024U &&
                     selftest_mkdirs(GEMOS_FILETEST_SUBDIR) &&
                     gemfs_write("/fstest/FILETEST.ELF",
                                 _binary_filetest_elf_start, size, 0,
                                 0) == (int)size,
                 "fs: the kernel wrote FILETEST.ELF (over 64 KB) to /fstest");
  pid = process_spawn_user_from_file("/fstest/FILETEST.ELF");
  ended = pid >= 0 && selftest_wait_exit(pid, &exit);
  selftest_begin(ended && !exit.faulted && exit.exit_code == GEMOS_FILETEST_OK,
                 "fs: FILETEST.ELF started from GemFS ");
  if (pid < 0) {
    serial_print("did not start\n");
  } else if (!ended) {
    serial_print("did not end\n");
    process_kill_pid((uint32_t)pid, -1);
  } else if (exit.faulted) {
    serial_print("faulted with vector ");
    serial_print_dec(exit.vector);
    serial_print("\n");
  } else if (exit.exit_code >= 0 &&
             (uint32_t)exit.exit_code <
                 sizeof(filetest_steps) / sizeof(filetest_steps[0])) {
    serial_print(filetest_steps[exit.exit_code]);
    serial_print("\n");
  } else {
    serial_print("exited with code ");
    serial_print_dec((uint32_t)exit.exit_code);
    serial_print("\n");
  }

  selftest_check(selftest_file_is("UTERM.ELF", _binary_uterm_image_bin_start,
                                  uterm_size) &&
                     gemfs_stat("UTERM.ELF", &stat) == GEMFS_OK &&
                     (stat.flags & GEMFS_FLAG_SYSTEM),
                 "fs: UTERM.ELF is unchanged after the process tried to "
                 "overwrite it");
  selftest_check(gemfs_stat("uterm.elf", &stat) == GEMFS_ERR_NOENT &&
                     gemfs_stat("/fstest/NEW.ELF", &stat) == GEMFS_ERR_NOENT,
                 "fs: the refused writes created no files");
  selftest_check(selftest_file_is(GEMOS_FILETEST_BIG, NULL,
                                  GEMOS_FILETEST_SIZE),
                 "fs: " GEMOS_FILETEST_BIG " written by FILETEST.ELF reads "
                 "back in the kernel");
}

static void selftest_fs(void) {
  if (!selftest_check(gemfs_available(), "fs: a GemFS disk is mounted")) {
    return;
  }
  selftest_fs_persist(); /* before FILETEST.ELF writes its files again */
  selftest_fs_api();
  selftest_fs_write_gate();
  selftest_fs_filetest();
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
  selftest_fs();

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
