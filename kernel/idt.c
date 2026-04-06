#include "idt.h"
#include <string.h> /* For memset */

/* Declare the IDT and its pointer */
idt_entry_t idt_entries[IDT_ENTRIES];
idt_ptr_t idt_ptr;

/* Defined in assembly (interrupts.S) usually, but we can do inline here for
 * simplicity */
static void idt_flush(uint32_t ptr) {
  __asm__ volatile("lidt (%0)" : : "r"(ptr));
}

void init_idt(void) {
  /* Set the IDT limit */
  idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
  idt_ptr.base = (uint32_t)(uintptr_t)&idt_entries;

  /* Initialize the IDT with zeros */
  memset(&idt_entries, 0, sizeof(idt_entry_t) * IDT_ENTRIES);

  /* Load the IDT */
  idt_flush((uint32_t)(uintptr_t)&idt_ptr);
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
  idt_entries[num].base_low = base & 0xFFFF;
  idt_entries[num].base_high = (base >> 16) & 0xFFFF;

  idt_entries[num].sel = sel;
  idt_entries[num].always0 = 0;

  /* We must uncomment the OR below when we get to using user-mode.
   * It sets the interrupt gate's privilege level to 3. */
  /* flags |= 0x60; */

  idt_entries[num].flags = flags;
}
