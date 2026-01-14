#include "idt.h"
#include "../drivers/io.h"
#include "../drivers/video.h"

idt_gate_t idt[ISR_HANDLERS_COUNT];
idt_register_t idt_reg;

// External assembly function to load IDT
extern void idt_load(uint32_t);

// Assembly Wrappers
extern void isr0();
extern void irq1();  // Keyboard
extern void irq12(); // Mouse

void set_idt_gate(int n, uint32_t handler) {
  idt[n].low_offset = (uint16_t)(handler & 0xFFFF);
  idt[n].selector = 0x08; // KERNEL_CODE_SEG_OFFSET
  idt[n].always0 = 0;
  // 0x8E = 1 (Present) 00 (Priv) 0 (0) 1110 (32-bit Interrupt Gate)
  idt[n].flags = 0x8E;
  idt[n].high_offset = (uint16_t)((handler >> 16) & 0xFFFF);
}

void init_idt() {
  set_idt_gate(33, (uint32_t)irq1);  // IRQ 1 -> Keyboard
  set_idt_gate(44, (uint32_t)irq12); // IRQ 12 -> Mouse (0x20 + 12 = 32+12=44)

  idt_reg.base = (uint32_t)&idt;
  idt_reg.limit = ISR_HANDLERS_COUNT * sizeof(idt_gate_t) - 1;

  // Remap PIC
  outb(0x20, 0x11);
  outb(0xA0, 0x11);
  outb(0x21, 0x20); // Master offset 0x20 (32)
  outb(0xA1, 0x28); // Slave offset 0x28 (40)
  outb(0x21, 0x04);
  outb(0xA1, 0x02);
  outb(0x21, 0x01);
  outb(0xA1, 0x01);

  // Masking: Enable only IRQ1 (Keyboard), IRQ2 (Cascade), IRQ12 (Mouse)
  // Master: IRQ1 (bit 1) and IRQ2 (bit 2) = 0000 0110 inverted = 1111 1001 =
  // 0xF9
  outb(0x21, 0xF9);
  // Slave: IRQ12 is IRQ 4 on slave (bit 4) = 0001 0000 inverted = 1110 1111 =
  // 0xEF
  outb(0xA1, 0xEF);

  idt_load((uint32_t)&idt_reg);

  // Enable Interrupts
  sti();

  // draw_string(10, 580, "IDT Initialized.", 0x00FF00); // Comment out debug in
  // case causing issues
}
