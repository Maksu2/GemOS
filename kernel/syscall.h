#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_VECTOR 0x80U

enum {
  SYS_exit = 0,
  SYS_yield = 1,
  SYS_debug_write = 2,
  SYS_getpid = 3,
  SYS_ticks_ms = 4,
};

void syscall_init(void);
void syscall_interrupt_handler(registers_t *regs);
uint32_t syscall_take_pending_resume_esp(void);

int copy_from_user(void *destination, const void *user_source, size_t length);
int copy_to_user(void *user_destination, const void *source, size_t length);
int copy_user_string(char *destination, const char *user_source, size_t max_length);

#endif /* SYSCALL_H */
