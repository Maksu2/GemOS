#include "process.h"

#include "console.h"
#include "elf.h"
#include "gdt.h"
#include "scheduler.h"

#include "../drivers/serial.h"
#include "fs/crc32.h"
#include "fs/gemfs.h"
#include "include/heap.h"
#include "include/irq.h"
#include "memory/kstack.h"
#ifdef GEMOS_SELFTEST
#include "selftest.h"
#endif
#include <string.h>

static process_t process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;

typedef enum {
  PROCESS_IMAGE_SOURCE_GEMFS,
  PROCESS_IMAGE_SOURCE_EMBEDDED,
} process_image_source_t;

extern uint8_t _binary_usrsmoke_elf_start[];
extern uint8_t _binary_usrsmoke_elf_end[];
extern uint8_t _binary_uterm_image_bin_start[];
extern uint8_t _binary_uterm_image_bin_end[];
extern uint8_t _binary_about_image_bin_start[];
extern uint8_t _binary_about_image_bin_end[];
extern uint8_t _binary_utextedit_image_bin_start[];
extern uint8_t _binary_utextedit_image_bin_end[];

typedef struct {
  const char *name;
  uint8_t *start;
  uint8_t *end;
} embedded_user_program_t;

static embedded_user_program_t embedded_user_programs[] = {
    {"USRSMOKE.ELF", _binary_usrsmoke_elf_start,
     _binary_usrsmoke_elf_end},
    {"UTERM.ELF", _binary_uterm_image_bin_start,
     _binary_uterm_image_bin_end},
    {"ABOUT.ELF", _binary_about_image_bin_start,
     _binary_about_image_bin_end},
    {"UTEXTEDIT.ELF", _binary_utextedit_image_bin_start,
     _binary_utextedit_image_bin_end},
};

#define EMBEDDED_PROGRAM_COUNT                                                 \
  (sizeof(embedded_user_programs) / sizeof(embedded_user_programs[0]))

#define PROCESS_INITIAL_FRAME_WORDS 16U

static process_t *process_find_by_pid(uint32_t pid) {
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    if (process_table[i].state != PROC_UNUSED && process_table[i].pid == pid) {
      return &process_table[i];
    }
  }
  return NULL;
}

static const embedded_user_program_t *process_find_embedded_program(
    const char *name) {
  if (name == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < EMBEDDED_PROGRAM_COUNT; ++i) {
    if (strcmp(name, embedded_user_programs[i].name) == 0) {
      return &embedded_user_programs[i];
    }
  }

  return NULL;
}

/* The program of that name in the kernel image, used where it is. */
static int process_embedded_image(const char *name, const uint8_t **image,
                                  size_t *image_size) {
  const embedded_user_program_t *embedded = process_find_embedded_program(name);

  if (embedded == NULL || embedded->end <= embedded->start) {
    return 0;
  }
  *image = embedded->start;
  *image_size = (size_t)(embedded->end - embedded->start);
  return 1;
}

/* The file read into a heap block that the caller frees, or NULL. Only
 * GemFS and the free heap limit the size of a program. */
static uint8_t *process_read_file(const char *name, size_t *image_size) {
  gemfs_stat_t stat;
  uint8_t *image;

  if (gemfs_stat(name, &stat) != GEMFS_OK || stat.type != GEMFS_TYPE_FILE ||
      stat.size == 0) {
    return NULL;
  }
  image = (uint8_t *)kalloc(stat.size);
  if (image == NULL) {
    return NULL;
  }
  if (gemfs_read(stat.inode, 0, image, stat.size) != (int)stat.size) {
    kfree(image);
    return NULL;
  }
  *image_size = stat.size;
  return image;
}

static process_t *process_allocate(void) {
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    if (process_table[i].state == PROC_UNUSED) {
      return &process_table[i];
    }
  }
  return NULL;
}

static void process_reset(process_t *process) {
  if (process == NULL) {
    return;
  }
  memset(process, 0, sizeof(*process));
}

static uint32_t process_build_initial_frame(process_t *process) {
  uint32_t *sp;

  if (process == NULL || process->kernel_stack_base == NULL) {
    return 0;
  }

  sp = (uint32_t *)(uintptr_t)process->kernel_stack_top;

  /* Must stay byte-for-byte aligned with scheduler_irq0_stub/registers_t. */
  *--sp = GDT_USER_DS;
  *--sp = (uint32_t)process->entry_esp;
  *--sp = 0x00000202U;
  *--sp = GDT_USER_CS;
  *--sp = (uint32_t)process->entry_eip;
  *--sp = 0;
  *--sp = 32;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = 0;
  *--sp = GDT_USER_DS;

  if ((size_t)((uint32_t *)(uintptr_t)process->kernel_stack_top - sp) !=
      PROCESS_INITIAL_FRAME_WORDS) {
    return 0;
  }

  return (uint32_t)(uintptr_t)sp;
}

static void process_destroy(process_t *process) {
  if (process == NULL) {
    return;
  }

  if (process->task_id != 0) {
    scheduler_release_task(process->task_id);
  }
  paging_destroy_address_space(&process->as);
  kstack_free(process->kernel_stack_slot);
  process_reset(process);
}

void process_init(void) {
  memset(process_table, 0, sizeof(process_table));
  next_pid = 1;
}

/* The file holds exactly these bytes. */
static int process_file_matches(uint32_t inode, const uint8_t *image,
                                uint32_t size) {
  static uint8_t chunk[GEMFS_BLOCK_SIZE];

  for (uint32_t offset = 0; offset < size; offset += GEMFS_BLOCK_SIZE) {
    uint32_t length = size - offset;

    if (length > GEMFS_BLOCK_SIZE) {
      length = GEMFS_BLOCK_SIZE;
    }
    if (gemfs_read(inode, offset, chunk, length) != (int)length ||
        memcmp(chunk, image + offset, length) != 0) {
      return 0;
    }
  }
  return 1;
}

/* Why the program has to be written to GemFS, or NULL if it is there. */
static const char *process_seed_reason(const embedded_user_program_t *program,
                                       uint32_t size, uint32_t version) {
  gemfs_stat_t stat;

  if (gemfs_stat(program->name, &stat) != GEMFS_OK) {
    return "missing";
  }
  if (stat.type != GEMFS_TYPE_FILE || !(stat.flags & GEMFS_FLAG_SYSTEM) ||
      stat.version != version || stat.size != size) {
    return "changed";
  }
  if (!process_file_matches(stat.inode, program->start, size)) {
    return "damaged";
  }
  return NULL;
}

/*
 * The programs in the kernel image become system files on GemFS: processes
 * cannot change them, and the version is the CRC-32 of the image. A program
 * is written only when it is missing, has another version or does not
 * match the image, so booting the same kernel again writes nothing.
 */
int process_seed_userland(void) {
  uint32_t seeded = 0;
  uint32_t current = 0;

  if (!gemfs_available()) {
    serial_print("[PROC] No file system: programs run from the kernel image\n");
    return 0;
  }

  for (size_t i = 0; i < EMBEDDED_PROGRAM_COUNT; ++i) {
    const embedded_user_program_t *program = &embedded_user_programs[i];
    uint32_t size = (uint32_t)(program->end - program->start);
    uint32_t version = crc32(program->start, size);
    const char *reason = process_seed_reason(program, size, version);
    int result;

    if (reason == NULL) {
      current++;
      continue;
    }
    result = gemfs_write(program->name, program->start, size,
                         GEMFS_FLAG_SYSTEM, version);
    if (result < 0) {
      serial_print("[PROC] Failed to seed ");
      serial_print(program->name);
      serial_print(": ");
      serial_print(gemfs_error(result));
      serial_print("\n");
      continue;
    }
    serial_print("[PROC] Seeded ");
    serial_print(program->name);
    serial_print(" (");
    serial_print(reason);
    serial_print(")\n");
    seeded++;
  }

  serial_print("[PROC] Programs on GemFS: ");
  serial_print_dec(seeded);
  serial_print(" seeded, ");
  serial_print_dec(current);
  serial_print(" up to date\n");
  return seeded + current == EMBEDDED_PROGRAM_COUNT;
}

/*
 * The stack a program starts with (userland/crt0.S): argc, then argc
 * pointers (argv) and a NULL pointer, with the strings above them. argv[0]
 * is the name the program was started by, argv[1] the optional argument.
 */
static int process_push_arguments(process_t *process, const char *name,
                                  const char *arg) {
  uint32_t block[4U + (2U * GEMFS_PATH_MAX) / 4U];
  const char *strings[2] = {name, arg};
  uint32_t argc = arg != NULL ? 2U : 1U;
  uint32_t size = 4U * (argc + 2U); /* argc, argv[], NULL */
  uint32_t offsets[2];
  uintptr_t base;
  uint32_t interrupt_state;

  for (uint32_t i = 0; i < argc; ++i) {
    size_t length = strlen(strings[i]) + 1U;

    if (length > GEMFS_PATH_MAX) {
      return 0;
    }
    offsets[i] = size;
    memcpy((uint8_t *)block + size, strings[i], length);
    size += (uint32_t)length;
  }
  size = (size + 3U) & ~3U;
  base = process->user_stack_top - size;
  block[0] = argc;
  for (uint32_t i = 0; i < argc; ++i) {
    block[1U + i] = (uint32_t)(base + offsets[i]);
  }
  block[1U + argc] = 0;

  interrupt_state = irq_save();
  paging_switch_directory(process->as.page_directory);
  memcpy((void *)base, block, size);
  paging_switch_directory(paging_get_directory());
  irq_restore(interrupt_state);
  process->entry_esp = base;
  return 1;
}

/* Start a program from its ELF image. A GemFS copy that the loader rejects
 * is replaced by the one in the kernel image, if there is one. */
static int process_spawn_loaded(const char *name, const char *arg,
                                const uint8_t *image, size_t image_size,
                                process_image_source_t image_source) {
  process_t *process;
  int task_id;
  uint32_t initial_esp;

  process = process_allocate();
  if (process == NULL) {
    serial_print("[PROC] No free process slots\n");
    return -1;
  }

  process_reset(process);
  process->pid = next_pid++;
  process->state = PROC_LOADING;
  process->task_id = 0;
  process->kernel_stack_slot = -1;

  if (!paging_create_address_space(&process->as)) {
    serial_print("[PROC] Failed to create address space\n");
    process_reset(process);
    return -1;
  }

  process->kernel_stack_slot = kstack_alloc();
  if (process->kernel_stack_slot < 0) {
    serial_print("[PROC] Failed to allocate kernel stack\n");
    paging_destroy_address_space(&process->as);
    process_reset(process);
    return -1;
  }
  process->kernel_stack_base = kstack_base(process->kernel_stack_slot);
  process->kernel_stack_top = kstack_top(process->kernel_stack_slot);

  if (!elf_load_into_process(process, image, image_size)) {
    if (image_source != PROCESS_IMAGE_SOURCE_GEMFS ||
        !process_embedded_image(name, &image, &image_size)) {
      process_destroy(process);
      return -1;
    }
    serial_print("[PROC] GemFS image rejected, retrying embedded: ");
    serial_print(name);
    serial_print("\n");
    if (!elf_load_into_process(process, image, image_size)) {
      process_destroy(process);
      return -1;
    }
  }

  if (!process_push_arguments(process, name, arg)) {
    serial_print("[PROC] Arguments too long\n");
    process_destroy(process);
    return -1;
  }

  initial_esp = process_build_initial_frame(process);
  if (initial_esp == 0) {
    serial_print("[PROC] Failed to build initial frame\n");
    process_destroy(process);
    return -1;
  }

  task_id = task_create_user(process, initial_esp);
  if (task_id < 0) {
    serial_print("[PROC] Failed to create user task\n");
    process_destroy(process);
    return -1;
  }

  process->task_id = (uint32_t)task_id;
  process->state = PROC_READY;

  serial_print("[PROC] Spawned PID=");
  serial_print_dec(process->pid);
  serial_print(" ");
  serial_print(name);
  serial_print("\n");

  return (int)process->pid;
}

int process_spawn_user_from_file(const char *name) {
  return process_spawn_user_with_arg(name, NULL);
}

int process_spawn_user_with_arg(const char *name, const char *arg) {
  const uint8_t *image;
  uint8_t *file;
  size_t image_size;
  int pid;

  if (name == NULL) {
    return -1;
  }

  file = process_read_file(name, &image_size);
  if (file != NULL) {
    pid = process_spawn_loaded(name, arg, file, image_size,
                               PROCESS_IMAGE_SOURCE_GEMFS);
    kfree(file); /* the loader copied the segments */
    return pid;
  }
  if (!process_embedded_image(name, &image, &image_size)) {
    serial_print("[PROC] Failed to read user image: ");
    serial_print(name);
    serial_print("\n");
    return -1;
  }
  return process_spawn_loaded(name, arg, image, image_size,
                              PROCESS_IMAGE_SOURCE_EMBEDDED);
}

#ifdef GEMOS_SELFTEST
int process_spawn_user_image(const char *name, const uint8_t *image,
                             size_t size) {
  if (name == NULL || image == NULL) {
    return -1;
  }
  return process_spawn_loaded(name, NULL, image, size,
                              PROCESS_IMAGE_SOURCE_EMBEDDED);
}
#endif

int process_kill_pid(uint32_t pid, int32_t exit_code) {
  process_t *process = process_find_by_pid(pid);

  if (process == NULL) {
    return 0;
  }
  if (process->state == PROC_ZOMBIE || process->state == PROC_FAULTED) {
    return 1;
  }
  if (!scheduler_kill_task(process->task_id)) {
    return 0;
  }

  process->state = PROC_ZOMBIE;
  process->exit_code = exit_code;
  return 1;
}

void process_wake(uint32_t pid) {
  process_t *process = process_find_by_pid(pid);

  if (process != NULL && process->task_id != 0) {
    scheduler_wake(process->task_id);
  }
}

void process_reap_zombies(void) {
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    process_t *process = &process_table[i];

    if (process->state != PROC_ZOMBIE && process->state != PROC_FAULTED) {
      continue;
    }

    console_destroy_for_pid(process->pid);
    if (process->state == PROC_ZOMBIE) {
      serial_print("[PROC] Reaped PID=");
      serial_print_dec(process->pid);
      serial_print(" exit=");
      serial_print_dec((uint32_t)process->exit_code);
      serial_print("\n");
    } else {
      serial_print("[PROC] Faulted PID=");
      serial_print_dec(process->pid);
      serial_print(" vec=");
      serial_print_dec(process->fault_vector);
      serial_print(" err=");
      serial_print_hex(process->fault_error);
      serial_print(" cr2=0x");
      serial_print_hex(process->fault_cr2);
      serial_print("\n");
    }
#ifdef GEMOS_SELFTEST
    selftest_process_exited(process);
#endif
    process_destroy(process);
  }
}
