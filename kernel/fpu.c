#include "fpu.h"

#include "../drivers/serial.h"

#include <string.h>

#define CR0_MP (1U << 1) /* WAIT/FWAIT honours TS */
#define CR0_EM (1U << 2) /* no FPU: FPU instructions raise #NM */
#define CR0_TS (1U << 3) /* task switched: the next FPU use raises #NM */
#define CR0_NE (1U << 5) /* FPU errors as #MF, not through IRQ13 */
#define CR4_OSFXSR (1U << 9)
#define CR4_OSXMMEXCPT (1U << 10)
#define CPUID1_EDX_FXSR (1U << 24)
#define CPUID1_EDX_SSE (1U << 25)

static int fpu_use_fxsr;
static fpu_state_t fpu_clean_state;

void fpu_init(void) {
  uint32_t eax = 1, ebx, ecx, edx, cr0, cr4;

  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  fpu_use_fxsr = (edx & CPUID1_EDX_FXSR) != 0;

  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 &= ~(CR0_EM | CR0_TS);
  cr0 |= CR0_MP | CR0_NE;
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

  if (fpu_use_fxsr) {
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSFXSR;
    if (edx & CPUID1_EDX_SSE) {
      cr4 |= CR4_OSXMMEXCPT;
    }
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
  }

  __asm__ volatile("fninit");
  memset(&fpu_clean_state, 0, sizeof(fpu_clean_state));
  fpu_save(&fpu_clean_state);
  fpu_restore(&fpu_clean_state);

  serial_print(fpu_use_fxsr ? "[FPU] Per-task state with FXSAVE\n"
                            : "[FPU] Per-task state with FNSAVE\n");
}

void fpu_init_state(fpu_state_t *state) {
  memcpy(state, &fpu_clean_state, sizeof(*state));
}

void fpu_save(fpu_state_t *state) {
  if (fpu_use_fxsr) {
    __asm__ volatile("fxsave %0" : "=m"(*state));
  } else {
    /* fnsave also reinitializes the FPU; fpu_restore follows anyway */
    __asm__ volatile("fnsave %0" : "=m"(*state));
  }
}

void fpu_restore(const fpu_state_t *state) {
  if (fpu_use_fxsr) {
    __asm__ volatile("fxrstor %0" : : "m"(*state));
  } else {
    __asm__ volatile("frstor %0" : : "m"(*state));
  }
}
