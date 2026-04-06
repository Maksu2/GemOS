#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../kernel/isr.h"
#include <stdint.h>

/* Initialize keyboard driver */
void init_keyboard(void);
uint8_t keyboard_get_last_key(void);

/* Keyboard IRQ Handler */
void keyboard_callback(registers_t *regs);

/* Basic Keycodes (Software independent) */
#define KEY_UNKNOWN 0
#define KEY_A 'A'
#define KEY_B 'B'
#define KEY_C 'C'
#define KEY_D 'D'
#define KEY_E 'E'
#define KEY_F 'F'
#define KEY_G 'G'
#define KEY_H 'H'
#define KEY_I 'I'
#define KEY_J 'J'
#define KEY_K 'K'
#define KEY_L 'L'
#define KEY_M 'M'
#define KEY_N 'N'
#define KEY_O 'O'
#define KEY_P 'P'
#define KEY_Q 'Q'
#define KEY_R 'R'
#define KEY_S 'S'
#define KEY_T 'T'
#define KEY_U 'U'
#define KEY_V 'V'
#define KEY_W 'W'
#define KEY_X 'X'
#define KEY_Y 'Y'
#define KEY_Z 'Z'
#define KEY_0 '0'
#define KEY_1 '1'
#define KEY_2 '2'
#define KEY_3 '3'
#define KEY_4 '4'
#define KEY_5 '5'
#define KEY_6 '6'
#define KEY_7 '7'
#define KEY_8 '8'
#define KEY_9 '9'
#define KEY_ENTER 0x0A
#define KEY_BACKSPACE 0x08
#define KEY_SPACE 0x20
#define KEY_ESC 0x1B
#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83

#endif
