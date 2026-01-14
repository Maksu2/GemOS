#ifndef IO_H
#define IO_H

#include "../kernel/types.h"

// Read a byte from external I/O port
static inline uint8_t inb(uint16_t port) {
  uint8_t result;
  __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
  return result;
}

// Write a byte to external I/O port
static inline void outb(uint16_t port, uint8_t data) {
  __asm__ volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

// Enable Interrupts
static inline void sti() { __asm__ volatile("sti"); }

// Disable Interrupts
static inline void cli() { __asm__ volatile("cli"); }

#endif
