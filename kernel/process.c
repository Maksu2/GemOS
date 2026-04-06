#include "process.h"

#include "console.h"
#include "elf.h"
#include "gdt.h"
#include "scheduler.h"

#include "../drivers/serial.h"
#include "fs/gemfs.h"
#include "include/heap.h"
#include <string.h>

static process_t process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint8_t process_file_buffer[GEMFS_MAX_FILESIZE];

extern uint8_t _binary_build_usrsmoke_elf_start[];
extern uint8_t _binary_build_usrsmoke_elf_end[];
extern uint8_t _binary_build_uterm_elf_start[];
extern uint8_t _binary_build_uterm_elf_end[];

typedef struct {
  const char *name;
  uint8_t *start;
  uint8_t *end;
} embedded_user_program_t;

static embedded_user_program_t embedded_user_programs[] = {
    {"USRSMOKE.ELF", _binary_build_usrsmoke_elf_start,
     _binary_build_usrsmoke_elf_end},
    {"UTERM.ELF", _binary_build_uterm_elf_start, _binary_build_uterm_elf_end},
};

#define PROCESS_INITIAL_FRAME_WORDS 16U

static int process_has_elf_magic(const uint8_t *image, size_t image_size) {
  return image != NULL && image_size >= 4 && image[0] == 0x7FU &&
         image[1] == 'E' && image[2] == 'L' && image[3] == 'F';
}

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

  for (size_t i = 0; i < sizeof(embedded_user_programs) / sizeof(embedded_user_programs[0]);
       ++i) {
    if (strcmp(name, embedded_user_programs[i].name) == 0) {
      return &embedded_user_programs[i];
    }
  }

  return NULL;
}

static int process_copy_embedded_image(const char *name, int *image_size) {
  const embedded_user_program_t *embedded = process_find_embedded_program(name);
  size_t blob_size;

  if (embedded == NULL || image_size == NULL) {
    return 0;
  }

  blob_size = (size_t)(embedded->end - embedded->start);
  if (blob_size == 0 || blob_size > sizeof(process_file_buffer)) {
    return 0;
  }

  memcpy(process_file_buffer, embedded->start, blob_size);
  *image_size = (int)blob_size;
  return 1;
}

static int process_load_image(const char *name, int *image_size) {
  int file_size;

  if (name == NULL || image_size == NULL) {
    return 0;
  }

  file_size =
      gemfs_read(name, (char *)process_file_buffer, sizeof(process_file_buffer));
  if (file_size > 0 &&
      process_has_elf_magic(process_file_buffer, (size_t)file_size)) {
    *image_size = file_size;
    return 1;
  }

  return process_copy_embedded_image(name, image_size);
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
  *--sp = (uint32_t)process->user_stack_top;
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
  if (process->kernel_stack_base != NULL) {
    kfree(process->kernel_stack_base);
  }
  process_reset(process);
}

void process_init(void) {
  memset(process_table, 0, sizeof(process_table));
  next_pid = 1;
}

int process_seed_userland(void) {
  int seeded = 0;

  for (size_t i = 0; i < sizeof(embedded_user_programs) / sizeof(embedded_user_programs[0]);
       ++i) {
    const embedded_user_program_t *program = &embedded_user_programs[i];
    size_t blob_size = (size_t)(program->end - program->start);

    if (blob_size == 0 || blob_size > GEMFS_MAX_FILESIZE) {
      serial_print("[PROC] Invalid embedded user image: ");
      serial_print(program->name);
      serial_print("\n");
      continue;
    }
    if (gemfs_write(program->name, (const char *)program->start,
                    (uint32_t)blob_size) < 0) {
      serial_print("[PROC] Failed to seed user image: ");
      serial_print(program->name);
      serial_print("\n");
      continue;
    }

    seeded = 1;
  }

  return seeded;
}

int process_spawn_user_from_file(const char *name) {
  process_t *process;
  int image_size;
  int task_id;
  uint32_t initial_esp;

  if (name == NULL) {
    return -1;
  }

  if (!process_load_image(name, &image_size)) {
    serial_print("[PROC] Failed to read user image: ");
    serial_print(name);
    serial_print("\n");
    return -1;
  }

  process = process_allocate();
  if (process == NULL) {
    serial_print("[PROC] No free process slots\n");
    return -1;
  }

  process_reset(process);
  process->pid = next_pid++;
  process->state = PROC_LOADING;
  process->task_id = 0;

  if (!paging_create_address_space(&process->as)) {
    serial_print("[PROC] Failed to create address space\n");
    process_reset(process);
    return -1;
  }

  process->kernel_stack_base = (uint8_t *)kalloc(TASK_STACK_SIZE);
  if (process->kernel_stack_base == NULL) {
    serial_print("[PROC] Failed to allocate kernel stack\n");
    paging_destroy_address_space(&process->as);
    process_reset(process);
    return -1;
  }
  process->kernel_stack_top =
      (uintptr_t)(process->kernel_stack_base + TASK_STACK_SIZE);

  if (!elf_load_into_process(process, process_file_buffer, (size_t)image_size)) {
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

void process_reap_zombies(void) {
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    process_t *process = &process_table[i];

    if (process->state == PROC_ZOMBIE) {
      console_destroy_for_pid(process->pid);
      serial_print("[PROC] Reaped PID=");
      serial_print_dec(process->pid);
      serial_print(" exit=");
      serial_print_dec((uint32_t)process->exit_code);
      serial_print("\n");
      process_destroy(process);
    } else if (process->state == PROC_FAULTED) {
      console_destroy_for_pid(process->pid);
      serial_print("[PROC] Faulted PID=");
      serial_print_dec(process->pid);
      serial_print(" vec=");
      serial_print_dec(process->fault_vector);
      serial_print(" err=");
      serial_print_hex(process->fault_error);
      serial_print(" cr2=0x");
      serial_print_hex(process->fault_cr2);
      serial_print("\n");
      process_destroy(process);
    }
  }
}
