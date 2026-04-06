/*
 * GemOS Serial Port Driver (COM1)
 *
 * Provides debug output via serial port.
 */

#include "serial.h"

/* Port offsets */
#define DATA_REG 0        /* Data register (read/write) */
#define INT_ENABLE_REG 1  /* Interrupt enable register */
#define BAUD_DIV_LOW 0    /* Baud rate divisor low byte (DLAB=1) */
#define BAUD_DIV_HIGH 1   /* Baud rate divisor high byte (DLAB=1) */
#define FIFO_CTRL_REG 2   /* FIFO control register */
#define LINE_CTRL_REG 3   /* Line control register */
#define MODEM_CTRL_REG 4  /* Modem control register */
#define LINE_STATUS_REG 5 /* Line status register */

/* Line status register bits */
#define LSR_TX_EMPTY 0x20 /* Transmitter holding register empty */

#include "../include/io.h"

/* ========================================================================= */
/* KLOG Ring Buffer - for Terminal access                                    */
/* ========================================================================= */

#define KLOG_MAX_LINES 64
#define KLOG_LINE_LEN 80

static char klog_buffer[KLOG_MAX_LINES][KLOG_LINE_LEN];
static int klog_write_idx = 0;  /* Next line to write */
static int klog_line_count = 0; /* Total lines written (capped at MAX) */

/* Current line being built */
static char klog_current_line[KLOG_LINE_LEN];
static int klog_current_pos = 0;

/* Flush current line to ring buffer */
static void klog_flush_line(void) {
  if (klog_current_pos == 0)
    return;

  /* Null-terminate */
  klog_current_line[klog_current_pos] = '\0';

  /* Copy to ring buffer */
  for (int i = 0; i <= klog_current_pos && i < KLOG_LINE_LEN; i++) {
    klog_buffer[klog_write_idx][i] = klog_current_line[i];
  }

  /* Advance write index (circular) */
  klog_write_idx = (klog_write_idx + 1) % KLOG_MAX_LINES;
  if (klog_line_count < KLOG_MAX_LINES) {
    klog_line_count++;
  }

  /* Reset current line */
  klog_current_pos = 0;
}

/* Add character to current line */
static void klog_putc(char c) {
  if (c == '\n' || c == '\r') {
    klog_flush_line();
  } else if (klog_current_pos < KLOG_LINE_LEN - 1) {
    klog_current_line[klog_current_pos++] = c;
  }
  /* Ignore if line too long */
}

/* Public API for Terminal */
int klog_get_line_count(void) { return klog_line_count; }

const char *klog_get_line(int index) {
  if (index < 0 || index >= klog_line_count)
    return "";

  /* Calculate actual buffer index */
  int start_idx;
  if (klog_line_count < KLOG_MAX_LINES) {
    start_idx = 0;
  } else {
    start_idx = klog_write_idx; /* Oldest line */
  }

  int actual_idx = (start_idx + index) % KLOG_MAX_LINES;
  return klog_buffer[actual_idx];
}

/* ========================================================================= */
/* Serial Port Driver                                                        */
/* ========================================================================= */

/* Initialize serial port */
void serial_init(void) {
  uint16_t port = COM1;

  /* Disable interrupts */
  outb(port + INT_ENABLE_REG, 0x00);

  /* Enable DLAB (set baud rate divisor) */
  outb(port + LINE_CTRL_REG, 0x80);

  /* Set baud rate to 115200 (divisor = 1) */
  outb(port + BAUD_DIV_LOW, 0x01);
  outb(port + BAUD_DIV_HIGH, 0x00);

  /* 8 bits, no parity, 1 stop bit */
  outb(port + LINE_CTRL_REG, 0x03);

  /* Enable FIFO, clear them, with 14-byte threshold */
  outb(port + FIFO_CTRL_REG, 0xC7);

  /* DTR + RTS + OUT2 */
  outb(port + MODEM_CTRL_REG, 0x0B);
}

/* Wait until transmitter is ready */
static void serial_wait_tx_ready(void) {
  while ((inb(COM1 + LINE_STATUS_REG) & LSR_TX_EMPTY) == 0) {
    /* Spin */
  }
}

/* Write a single character */
void serial_putc(char c) {
  serial_wait_tx_ready();
  outb(COM1 + DATA_REG, c);
  klog_putc(c); /* Also store in ring buffer */
}

/* Write a null-terminated string */
void serial_print(const char *str) {
  while (*str) {
    if (*str == '\n') {
      serial_putc('\r');
    }
    serial_putc(*str++);
  }
}

/* Print a decimal number */
void serial_print_dec(uint32_t value) {
  char buffer[12];
  int i = 0;

  if (value == 0) {
    serial_putc('0');
    return;
  }

  while (value > 0) {
    buffer[i++] = '0' + (value % 10);
    value /= 10;
  }

  while (--i >= 0) {
    serial_putc(buffer[i]);
  }
}

/* Print a hexadecimal number */
void serial_print_hex(uint32_t value) {
  const char *hex = "0123456789ABCDEF";
  char buffer[9];
  int i;

  for (i = 7; i >= 0; i--) {
    buffer[i] = hex[value & 0xF];
    value >>= 4;
  }
  buffer[8] = '\0';

  serial_print(buffer);
}
