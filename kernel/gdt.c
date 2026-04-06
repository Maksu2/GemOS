#include "gdt.h"

#include "../drivers/serial.h"
#include <string.h>

#define GDT_ENTRY_COUNT 6

static gdt_entry_t gdt_entries[GDT_ENTRY_COUNT];
static gdt_ptr_t gdt_ptr;
static tss32_t kernel_tss;

extern void gdt_flush(const gdt_ptr_t *ptr);
extern void tss_flush(uint16_t selector);

static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
  gdt_entries[index].base_low = (uint16_t)(base & 0xFFFFU);
  gdt_entries[index].base_middle = (uint8_t)((base >> 16) & 0xFFU);
  gdt_entries[index].base_high = (uint8_t)((base >> 24) & 0xFFU);

  gdt_entries[index].limit_low = (uint16_t)(limit & 0xFFFFU);
  gdt_entries[index].granularity =
      (uint8_t)(((limit >> 16) & 0x0FU) | (granularity & 0xF0U));
  gdt_entries[index].access = access;
}

static void gdt_write_tss(int index, uint32_t base, uint32_t limit) {
  gdt_set_entry(index, base, limit, 0x89U, 0x00U);
}

void gdt_set_kernel_stack(uint32_t stack_top) { kernel_tss.esp0 = stack_top; }

const tss32_t *gdt_get_tss(void) { return &kernel_tss; }

void gdt_init(void) {
  uint32_t current_esp;

  memset(&gdt_entries, 0, sizeof(gdt_entries));
  memset(&kernel_tss, 0, sizeof(kernel_tss));

  gdt_ptr.limit = (uint16_t)(sizeof(gdt_entries) - 1U);
  gdt_ptr.base = (uint32_t)(uintptr_t)&gdt_entries[0];

  gdt_set_entry(0, 0, 0, 0, 0);
  gdt_set_entry(1, 0, 0xFFFFFU, 0x9AU, 0xCFU);
  gdt_set_entry(2, 0, 0xFFFFFU, 0x92U, 0xCFU);
  gdt_set_entry(3, 0, 0xFFFFFU, 0xFAU, 0xCFU);
  gdt_set_entry(4, 0, 0xFFFFFU, 0xF2U, 0xCFU);

  __asm__ volatile("mov %%esp, %0" : "=r"(current_esp));
  kernel_tss.ss0 = GDT_KERNEL_DS;
  kernel_tss.esp0 = current_esp;
  kernel_tss.iomap_base = sizeof(kernel_tss);
  gdt_write_tss(5, (uint32_t)(uintptr_t)&kernel_tss,
                (uint32_t)(sizeof(kernel_tss) - 1U));

  gdt_flush(&gdt_ptr);
  tss_flush((uint16_t)GDT_TSS_SEL);

  serial_print("[GDT] Kernel GDT loaded: KCS=0x");
  serial_print_hex(GDT_KERNEL_CS);
  serial_print(" KDS=0x");
  serial_print_hex(GDT_KERNEL_DS);
  serial_print(" UCS=0x");
  serial_print_hex(GDT_USER_CS);
  serial_print(" UDS=0x");
  serial_print_hex(GDT_USER_DS);
  serial_print(" TSS=0x");
  serial_print_hex(GDT_TSS_SEL);
  serial_print("\n");
  serial_print("[TSS] Loaded: esp0=0x");
  serial_print_hex(kernel_tss.esp0);
  serial_print(" ss0=0x");
  serial_print_hex(kernel_tss.ss0);
  serial_print("\n");
}
