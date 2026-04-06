#ifndef PROCESS_H
#define PROCESS_H

#include "scheduler.h"
#include "memory/paging.h"

#include <stdint.h>

#define MAX_PROCESSES MAX_TASKS

typedef enum {
  PROC_UNUSED = 0,
  PROC_LOADING,
  PROC_READY,
  PROC_RUNNING,
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
  uint8_t *kernel_stack_base;
  uintptr_t kernel_stack_top;
  int32_t exit_code;
  uint32_t fault_vector;
  uint32_t fault_error;
  uint32_t fault_cr2;
} process_t;

void process_init(void);
int process_seed_userland(void);
int process_spawn_user_from_file(const char *name);
void process_reap_zombies(void);

#endif /* PROCESS_H */
