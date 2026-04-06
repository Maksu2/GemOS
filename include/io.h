#ifndef IO_H
#define IO_H

#include <stdint.h>

/* Write 8-bit value to I/O port */
static inline void outb(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read 8-bit value from I/O port */
static inline uint8_t inb(uint16_t port) {
  uint8_t value;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

/* Write 16-bit value to I/O port */
static inline void outw(uint16_t port, uint16_t value) {
  __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Read 16-bit value from I/O port */
static inline uint16_t inw(uint16_t port) {
  uint16_t value;
  __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

/* Wait for I/O operation to complete */
static inline void io_wait(void) {
  /* Write to unused port 0x80 */
  outb(0x80, 0);
}

#endif /* IO_H */
