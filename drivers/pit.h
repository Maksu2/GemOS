#ifndef PIT_H
#define PIT_H

#include "../kernel/isr.h"
#include <stdint.h>

/* PIT Ports */
#define PIT_CH0 0x40 /* Channel 0 Data Port */
#define PIT_CH1 0x41 /* Channel 1 Data Port */
#define PIT_CH2 0x42 /* Channel 2 Data Port */
#define PIT_CMD 0x43 /* Command Register */

/* Frequency of the global oscillator */
#define PIT_BASE_FREQ 1193182

/* Target Frequency */
#define PIT_FREQ 1000

/* Initialization function */
void init_pit(void);

/* Get current tick count */
uint64_t timer_get_ticks(void);

/* Callback for IRQ0 (used before scheduler_init) */
void timer_callback(registers_t *regs);

/* Core tick logic — called directly by scheduler_tick after scheduler_init */
void pit_tick(void);

#endif
