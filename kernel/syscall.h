#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include "../include/gemos/syscall_abi.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_VECTOR GEMOS_SYSCALL_VECTOR

void syscall_init(void);
void syscall_interrupt_handler(registers_t *regs);
uint32_t syscall_take_pending_resume_esp(void);

int copy_from_user(void *destination, const void *user_source, size_t length);
int copy_to_user(void *user_destination, const void *source, size_t length);
int copy_user_string(char *destination, const char *user_source, size_t max_length);

#endif /* SYSCALL_H */
