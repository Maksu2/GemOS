/*
 * GemOS Serial Port Driver (COM1)
 *
 * Provides debug output via serial port.
 * Output can be viewed in QEMU with: -serial stdio
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* Serial port base addresses */
#define COM1 0x3F8
#define COM2 0x2F8

/* Initialize serial port */
void serial_init(void);

/* Write a single character */
void serial_putc(char c);

/* Write a null-terminated string */
void serial_print(const char *str);

/* Print a decimal number */
void serial_print_dec(uint32_t value);

/* Print a hexadecimal number */
void serial_print_hex(uint32_t value);

/* ========================================================================= */
/* KLOG API - Ring buffer access for Terminal                                */
/* ========================================================================= */

/* Get number of log lines available */
int klog_get_line_count(void);

/* Get log line by index (0 = oldest available) */
const char *klog_get_line(int index);

#endif /* SERIAL_H */
