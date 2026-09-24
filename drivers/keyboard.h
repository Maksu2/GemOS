#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../kernel/isr.h"
#include <gemos/console_abi.h>
#include <stdint.h>

/* Initialize keyboard driver */
void init_keyboard(void);

/* Keyboard IRQ Handler. Key press events carry ASCII characters or the
 * GEMOS_KEY_* codes from <gemos/console_abi.h>, the same values userland
 * receives through the console ABI. */
void keyboard_callback(registers_t *regs);

#endif
