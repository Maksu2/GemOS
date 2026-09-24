#include "kstack.h"

#include "../../drivers/serial.h"
#include "../include/irq.h"
#include "../process.h"
#include "paging.h"

/* Task 0 switches to this stack in kernel/entry.S, right after the BSS is
 * zeroed. */
uint8_t kstack_boot[PAGE_SIZE + KSTACK_BOOT_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
/* read by entry.S after the BSS is cleared (lives in .rodata) */
uint8_t *const kstack_boot_top = kstack_boot + sizeof(kstack_boot);
static uint8_t kstack_idle[PAGE_SIZE + KSTACK_SMALL_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t kstack_double_fault[PAGE_SIZE + KSTACK_SMALL_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t kstack_process[MAX_PROCESSES][PAGE_SIZE + TASK_STACK_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t kstack_used[MAX_PROCESSES];

void kstack_init(void) {
  /* the first page of every stack area is the guard */
  paging_unmap_kernel_page((uintptr_t)kstack_boot);
  paging_unmap_kernel_page((uintptr_t)kstack_idle);
  paging_unmap_kernel_page((uintptr_t)kstack_double_fault);
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    paging_unmap_kernel_page((uintptr_t)kstack_process[i]);
  }
  serial_print("[KSTACK] Guard pages below all kernel stacks\n");
}

uintptr_t kstack_idle_top(void) {
  return (uintptr_t)kstack_idle + sizeof(kstack_idle);
}

uintptr_t kstack_double_fault_top(void) {
  return (uintptr_t)kstack_double_fault + sizeof(kstack_double_fault);
}

int kstack_alloc(void) {
  uint32_t flags = irq_save();

  for (int i = 0; i < MAX_PROCESSES; ++i) {
    if (!kstack_used[i]) {
      kstack_used[i] = 1;
      irq_restore(flags);
      return i;
    }
  }
  irq_restore(flags);
  return -1;
}

void kstack_free(int slot) {
  if (slot >= 0 && slot < MAX_PROCESSES) {
    kstack_used[slot] = 0;
  }
}

uint8_t *kstack_base(int slot) { return kstack_process[slot] + PAGE_SIZE; }

uintptr_t kstack_top(int slot) {
  return (uintptr_t)kstack_process[slot] + sizeof(kstack_process[slot]);
}

static int kstack_in_guard(const uint8_t *area, uintptr_t addr) {
  return addr >= (uintptr_t)area && addr < (uintptr_t)area + PAGE_SIZE;
}

int kstack_is_guard(uintptr_t addr) {
  if (kstack_in_guard(kstack_boot, addr) || kstack_in_guard(kstack_idle, addr) ||
      kstack_in_guard(kstack_double_fault, addr)) {
    return 1;
  }
  for (int i = 0; i < MAX_PROCESSES; ++i) {
    if (kstack_in_guard(kstack_process[i], addr)) {
      return 1;
    }
  }
  return 0;
}

uint32_t kstack_boot_high_water(void) {
  const uint8_t *bottom = kstack_boot + PAGE_SIZE;
  uint32_t untouched = 0;

  while (untouched < KSTACK_BOOT_SIZE && bottom[untouched] == 0) {
    untouched++;
  }
  return KSTACK_BOOT_SIZE - untouched;
}
