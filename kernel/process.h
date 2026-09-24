#ifndef PROCESS_H
#define PROCESS_H

#include "scheduler.h"
#include "memory/paging.h"

#include <stddef.h>
#include <stdint.h>

#define MAX_PROCESSES MAX_TASKS

typedef enum {
  PROC_UNUSED = 0,
  PROC_LOADING,
  PROC_READY,
  PROC_RUNNING,
  PROC_BLOCKED,
  PROC_ZOMBIE,
  PROC_FAULTED,
} process_state_t;

typedef struct process {
  uint32_t pid;
  uint32_t task_id;
  process_state_t state;
  address_space_t as;
  uintptr_t entry_eip;
  uintptr_t image_base;
  uintptr_t image_end;
  uintptr_t user_stack_top;
  uintptr_t user_stack_bottom;
  int kernel_stack_slot; /* kstack_alloc(), -1 when none */
  uint8_t *kernel_stack_base;
  uintptr_t kernel_stack_top;
  int32_t exit_code;
  /* SYS_console_wait_event in progress: the syscall restarts until an event
   * arrives or the tick count reaches wait_deadline (0 = no timeout). */
  int waiting;
  uint64_t wait_deadline;
  uint32_t fault_vector;
  uint32_t fault_error;
  uint32_t fault_cr2;
} process_t;

void process_init(void);
int process_seed_userland(void);
int process_spawn_user_from_file(const char *name);
#ifdef GEMOS_SELFTEST
/* Start a program from an ELF image in memory (at most 8 KB). */
int process_spawn_user_image(const char *name, const uint8_t *image,
                             size_t size);
#endif
int process_kill_pid(uint32_t pid, int32_t exit_code);
/* Make the process runnable if it is blocked (e.g. waiting for an event). */
void process_wake(uint32_t pid);
void process_reap_zombies(void);

#endif /* PROCESS_H */
