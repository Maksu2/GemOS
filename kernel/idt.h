#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Number of interrupt handlers */
#define IDT_ENTRIES 256

/* IDT Entry structure (Gate Descriptor) */
typedef struct {
  uint16_t base_low;  /* Lower 16 bits of handler address */
  uint16_t sel;       /* Kernel segment selector */
  uint8_t always0;    /* This must always be 0 */
  uint8_t flags;      /* Type and attributes */
  uint16_t base_high; /* Upper 16 bits of handler address */
} __attribute__((packed)) idt_entry_t;

/* IDT Pointer structure (for LIDT instruction) */
typedef struct {
  uint16_t limit;
  uint32_t base; /* The address of the first element in our idt_entry_t array */
} __attribute__((packed)) idt_ptr_t;

/* Functions */
void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif /* IDT_H */
