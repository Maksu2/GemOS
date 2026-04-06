#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Registers state after ISR interrupt */
typedef struct {
  uint32_t ds;                                     /* Data segment selector */
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* Pushed by pusha */
  uint32_t int_no,
      err_code; /* Interrupt number and error code (if applicable) */
  uint32_t eip, cs, eflags, useresp,
      ss; /* Pushed by the processor automatically */
} registers_t;

/* Initialize ISR handlers */
void init_isr(void);

/* Interrupt handler type */
typedef void (*isr_t)(registers_t *);

/* Register an interrupt handler */
void register_interrupt_handler(uint8_t n, isr_t handler);

#endif /* ISR_H */
