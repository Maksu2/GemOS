#ifndef FPU_H
#define FPU_H

#include <stdint.h>

/*
 * x87/SSE state of each task. The scheduler saves the state of the task it
 * leaves and restores the one of the task it resumes on every switch
 * (FXSAVE/FXRSTOR, or FNSAVE/FRSTOR on CPUs without FXSR). Interrupt
 * handlers do not touch the FPU (see IRQ_PATH_SOURCES in the Makefile).
 */
#define FPU_STATE_SIZE 512 /* FXSAVE area; FNSAVE needs 108 bytes */

/* Areas passed to the functions below must be 16-byte aligned. */
typedef struct {
  uint8_t bytes[FPU_STATE_SIZE];
} __attribute__((aligned(16))) fpu_state_t;

/* Enable the FPU (and SSE state if the CPU has FXSR) and record the clean
 * state new tasks start from. Must run before any floating point code. */
void fpu_init(void);

void fpu_init_state(fpu_state_t *state);
void fpu_save(fpu_state_t *state);
void fpu_restore(const fpu_state_t *state);

#endif /* FPU_H */
