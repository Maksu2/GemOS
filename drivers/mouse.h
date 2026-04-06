#ifndef MOUSE_H
#define MOUSE_H

#include "../kernel/isr.h"
#include <stdint.h>

/* Mouse State */
typedef struct {
  int32_t x;
  int32_t y;
  uint8_t flags;
  uint8_t buttons;
} mouse_state_t;

/* Initialize mouse driver */
void init_mouse(void);

/* Mouse IRQ Handler */
void mouse_callback(registers_t *regs);

/* Helpers */
void mouse_wait(uint8_t type);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);

#endif
