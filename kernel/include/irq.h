#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/*
 * Short critical sections for state shared with interrupt handlers.
 * Kernel code is never preempted (see kernel/scheduler.c), so these are
 * only needed around data that an IRQ handler also touches.
 */

/* Disable interrupts; returns the previous EFLAGS for irq_restore(). */
static inline uint32_t irq_save(void) {
  uint32_t flags;
  __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
  return flags;
}

/* Re-enable interrupts if they were enabled when irq_save() ran. */
static inline void irq_restore(uint32_t flags) {
  if (flags & 0x200U) {
    __asm__ volatile("sti" : : : "memory");
  }
}

#endif /* IRQ_H */
