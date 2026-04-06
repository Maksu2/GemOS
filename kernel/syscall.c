#include "syscall.h"

#include "gdt.h"
#include "idt.h"
#include "memory/paging.h"
#include "process.h"
#include "scheduler.h"

#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include <string.h>

#define SYSCALL_DEBUG_WRITE_MAX 256U
#define SYSCALL_EINVAL ((uint32_t)-1)
#define SYSCALL_EFAULT ((uint32_t)-2)
#define SYSCALL_ENOSYS ((uint32_t)-3)
#define SYSCALL_E2BIG  ((uint32_t)-4)

static uint32_t pending_resume_esp = 0;

extern void isr128(void);

static process_t *syscall_current_process(void) {
  return scheduler_get_current_process();
}

uint32_t syscall_take_pending_resume_esp(void) {
  uint32_t resume_esp = pending_resume_esp;
  pending_resume_esp = 0;
  return resume_esp;
}

int copy_from_user(void *destination, const void *user_source, size_t length) {
  process_t *process = syscall_current_process();

  if (length == 0) {
    return 1;
  }
  if (process == NULL || destination == NULL || user_source == NULL) {
    return 0;
  }
  if (!paging_is_user_range_mapped(process->as.page_directory,
                                   (uintptr_t)user_source, length, 0)) {
    return 0;
  }

  memcpy(destination, user_source, length);
  return 1;
}

int copy_to_user(void *user_destination, const void *source, size_t length) {
  process_t *process = syscall_current_process();

  if (length == 0) {
    return 1;
  }
  if (process == NULL || user_destination == NULL || source == NULL) {
    return 0;
  }
  if (!paging_is_user_range_mapped(process->as.page_directory,
                                   (uintptr_t)user_destination, length, 1)) {
    return 0;
  }

  memcpy(user_destination, source, length);
  return 1;
}

int copy_user_string(char *destination, const char *user_source, size_t max_length) {
  if (destination == NULL || user_source == NULL || max_length == 0) {
    return 0;
  }

  for (size_t i = 0; i < max_length; ++i) {
    char ch;

    if (!copy_from_user(&ch, user_source + i, 1)) {
      return 0;
    }
    destination[i] = ch;
    if (ch == '\0') {
      return 1;
    }
  }

  destination[max_length - 1] = '\0';
  return 1;
}

static uint32_t syscall_debug_write(uintptr_t user_buffer, size_t length) {
  char scratch[SYSCALL_DEBUG_WRITE_MAX + 1];

  if (length > SYSCALL_DEBUG_WRITE_MAX) {
    return SYSCALL_E2BIG;
  }
  if (!copy_from_user(scratch, (const void *)user_buffer, length)) {
    return SYSCALL_EFAULT;
  }

  scratch[length] = '\0';
  serial_print("[USER] ");
  serial_print(scratch);
  return (uint32_t)length;
}

void syscall_interrupt_handler(registers_t *regs) {
  pending_resume_esp = 0;

  switch (regs->eax) {
  case SYS_exit:
    scheduler_mark_current_zombie((int32_t)regs->ebx);
    pending_resume_esp = scheduler_switch_now((uint32_t)(uintptr_t)regs);
    break;
  case SYS_yield:
    regs->eax = 0;
    pending_resume_esp = scheduler_yield_now((uint32_t)(uintptr_t)regs);
    break;
  case SYS_debug_write:
    regs->eax = syscall_debug_write((uintptr_t)regs->ebx, (size_t)regs->ecx);
    break;
  case SYS_getpid:
    regs->eax = (uint32_t)scheduler_get_current_pid();
    break;
  case SYS_ticks_ms:
    regs->eax = (uint32_t)timer_get_ticks();
    break;
  default:
    regs->eax = SYSCALL_ENOSYS;
    break;
  }
}

void syscall_init(void) {
  idt_set_gate(SYSCALL_VECTOR, (uint32_t)(uintptr_t)isr128, GDT_KERNEL_CS, 0xEE);
  register_interrupt_handler(SYSCALL_VECTOR, syscall_interrupt_handler);
  serial_print("[SYSCALL] int 0x80 ready\n");
}
