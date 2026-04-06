#include "process.h"

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

static int process_has_elf_magic(const uint8_t *image, size_t image_size) {
  return image != NULL && image_size >= 4 && image[0] == 0x7FU &&
         image[1] == 'E' && image[2] == 'L' && image[3] == 'F';
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

  /* Match scheduler_irq0_stub/registers_t exactly:
   * ds, edi, esi, ebp, esp_pre, ebx, edx, ecx, eax,
   * int_no, err_code, eip, cs, eflags, useresp, ss
   */
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
  const size_t blob_size =
      (size_t)(_binary_build_usrsmoke_elf_end - _binary_build_usrsmoke_elf_start);

  if (blob_size == 0 || blob_size > GEMFS_MAX_FILESIZE) {
    serial_print("[PROC] USRSMOKE blob invalid\n");
    return 0;
  }
  if (gemfs_write("USRSMOKE.ELF", (const char *)_binary_build_usrsmoke_elf_start,
                  (uint32_t)blob_size) < 0) {
    serial_print("[PROC] Failed to seed USRSMOKE.ELF\n");
    return 0;
  }

  serial_print("[PROC] Refreshed USRSMOKE.ELF in GemFS\n");
  return 1;
}

int process_spawn_user_from_file(const char *name) {
  process_t *process;
  int file_size;
  int task_id;
  uint32_t initial_esp;

  if (name == NULL) {
    return -1;
  }

  serial_print("[PROC] Spawning from GemFS: ");
  serial_print(name);
  serial_print("\n");

  file_size = gemfs_read(name, (char *)process_file_buffer, sizeof(process_file_buffer));
  if (file_size <= 0) {
    serial_print("[PROC] Failed to read user image: ");
    serial_print(name);
    serial_print("\n");
    return -1;
  }
  serial_print("[PROC] Read image bytes=");
  serial_print_dec((uint32_t)file_size);
  serial_print("\n");
  if (!process_has_elf_magic(process_file_buffer, (size_t)file_size) &&
      strcmp(name, "USRSMOKE.ELF") == 0) {
    size_t blob_size =
        (size_t)(_binary_build_usrsmoke_elf_end - _binary_build_usrsmoke_elf_start);

    if (blob_size > 0 && blob_size <= sizeof(process_file_buffer)) {
      memcpy(process_file_buffer, _binary_build_usrsmoke_elf_start, blob_size);
      file_size = (int)blob_size;
      serial_print("[PROC] GemFS payload invalid, using embedded fallback\n");
    }
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
  serial_print("[PROC] Address space created cr3=0x");
  serial_print_hex((uint32_t)process->as.cr3);
  serial_print("\n");

  process->kernel_stack_base = (uint8_t *)kalloc(TASK_STACK_SIZE);
  if (process->kernel_stack_base == NULL) {
    serial_print("[PROC] Failed to allocate kernel stack\n");
    paging_destroy_address_space(&process->as);
    process_reset(process);
    return -1;
  }
  process->kernel_stack_top =
      (uintptr_t)(process->kernel_stack_base + TASK_STACK_SIZE);
  serial_print("[PROC] Kernel stack top=0x");
  serial_print_hex((uint32_t)process->kernel_stack_top);
  serial_print("\n");

  serial_print("[PROC] Loading ELF...\n");
  if (!elf_load_into_process(process, process_file_buffer, (size_t)file_size)) {
    process_destroy(process);
    return -1;
  }
  serial_print("[PROC] ELF loaded\n");

  initial_esp = process_build_initial_frame(process);
  if (initial_esp == 0) {
    serial_print("[PROC] Failed to build initial frame\n");
    process_destroy(process);
    return -1;
  }
  serial_print("[PROC] Initial ESP=0x");
  serial_print_hex(initial_esp);
  serial_print("\n");

  task_id = task_create_user(process, initial_esp);
  if (task_id < 0) {
    serial_print("[PROC] Failed to create user task\n");
    process_destroy(process);
    return -1;
  }
  serial_print("[PROC] User task slot=");
  serial_print_dec((uint32_t)task_id);
  serial_print("\n");

  process->task_id = (uint32_t)task_id;
  process->state = PROC_READY;

  serial_print("[PROC] Spawned PID=");
  serial_print_dec(process->pid);
  serial_print(" task=");
  serial_print_dec((uint32_t)task_id);
  serial_print(" entry=0x");
  serial_print_hex((uint32_t)process->entry_eip);
  serial_print("\n");

  return (int)process->pid;
}

void process_reap_zombies(void) {
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    process_t *process = &process_table[i];

    if (process->state == PROC_ZOMBIE) {
      serial_print("[PROC] Reaped PID=");
      serial_print_dec(process->pid);
      serial_print(" exit=");
      serial_print_dec((uint32_t)process->exit_code);
      serial_print("\n");
      process_destroy(process);
    } else if (process->state == PROC_FAULTED) {
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
