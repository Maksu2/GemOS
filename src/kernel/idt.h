#ifndef IDT_H
#define IDT_H

#include "../kernel/types.h"

#define ISR_HANDLERS_COUNT 256

// IDT Entry Structure
typedef struct {
  uint16_t low_offset;  // Lower 16 bits of handler function address
  uint16_t selector;    // Kernel segment selector
  uint8_t always0;      // Always 0
  uint8_t flags;        // Type and attributes
  uint16_t high_offset; // Higher 16 bits of handler function address
} __attribute__((packed)) idt_gate_t;

// IDT Pointer Structure
typedef struct {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) idt_register_t;

// Registers struct
typedef struct {
  uint32_t ds;
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
  uint32_t int_no, err_code;
  uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

void init_idt();
void set_idt_gate(int n, uint32_t handler);

// External defined in interrupts.asm
extern void idt_load(uint32_t);
extern void irq1();  // Keyboard
extern void irq12(); // Mouse
extern void irq_handler(registers_t r);

#endif
