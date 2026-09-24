#ifndef KSTACK_H
#define KSTACK_H

#include <stdint.h>

/*
 * Kernel stacks. Each one has an unmapped guard page right below it, so an
 * overflow faults instead of overwriting whatever lies below. The fault
 * then happens on the overflowing stack itself and turns into a double
 * fault, which runs as a separate hardware task on its own stack
 * (gdt_init_double_fault) and reports the overflow.
 */
#define KSTACK_BOOT_SIZE (64U * 1024U)  /* task 0: GUI loop, font rendering */
#define KSTACK_SMALL_SIZE (8U * 1024U)  /* idle task, double fault handler */

/* Unmap the guard pages; paging must be on. */
void kstack_init(void);

uintptr_t kstack_idle_top(void);
uintptr_t kstack_double_fault_top(void);

/* Process kernel stacks (TASK_STACK_SIZE each). Returns the slot, -1 if
 * all are taken. */
int kstack_alloc(void);
void kstack_free(int slot);
uint8_t *kstack_base(int slot);
uintptr_t kstack_top(int slot);

/* Non-zero if addr lies in one of the guard pages. */
int kstack_is_guard(uintptr_t addr);

/* Bytes of the task 0 stack ever touched (stacks start zeroed). */
uint32_t kstack_boot_high_water(void);

#endif /* KSTACK_H */
