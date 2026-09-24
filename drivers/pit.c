#include "pit.h"
#include "../include/io.h"
#include "../kernel/include/event.h"
#include "../kernel/include/irq.h"
#include "../kernel/isr.h"
#include "pic.h"
#include "serial.h"

/* Global tick counter */
static volatile uint64_t global_ticks = 0;

/* Core timer logic — called from scheduler_tick (bypasses isr_handler path) */
void pit_tick(void) {
  global_ticks++;

  /* Push Timer Event every 10 ticks (10ms) to avoid spamming the queue */
  if (global_ticks % 10 == 0) {
    event_t ev;
    ev.type = EVENT_TIMER_TICK;
    ev.data.timer.tick_count = global_ticks;
    event_push(ev);
  }
}

/* The count of channel 0, latched so that both bytes belong together.
 * Interrupt handlers do not touch the PIT, so the sequence needs no lock. */
static uint16_t pit_read_count(void) {
  uint8_t low;
  uint8_t high;

  outb(PIT_CMD, 0x00); /* latch channel 0 */
  low = inb(PIT_CH0);
  high = inb(PIT_CH0);
  return (uint16_t)(low | (high << 8));
}

void pit_stopwatch_start(pit_stopwatch_t *watch) {
  watch->last = pit_read_count();
  watch->reloads = 0;
}

uint32_t pit_stopwatch_ms(pit_stopwatch_t *watch) {
  uint16_t now = pit_read_count();

  if (now > watch->last) {
    watch->reloads++; /* it counts down, so it went up: a reload */
  }
  watch->last = now;
  return watch->reloads / 2U;
}

/* IRQ0 Handler - System Timer (used before scheduler_init overrides IDT gate) */
void timer_callback(registers_t *regs) {
  (void)regs;
  pit_tick();
}

/* Initialize PIT Channel 0 */
void init_pit(void) {
  /* Calculate divisor for 1000 Hz */
  /* 1193182 Hz / 1000 Hz = 1193 */
  uint32_t divisor = PIT_BASE_FREQ / PIT_FREQ;

  /* Send Command Byte to Port 0x43 */
  /* 00 (Channel 0) | 11 (Access Lo/Hi) | 011 (Mode 3 Square Wave) | 0 (Binary)
   */
  /* 0x36 */
  outb(PIT_CMD, 0x36);

  /* Send Divisor Low Byte */
  outb(PIT_CH0, (uint8_t)(divisor & 0xFF));

  /* Send Divisor High Byte */
  outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

  /* Register IRQ0 Handler (Vector 32) */
  register_interrupt_handler(32, timer_callback);

  /* Enable IRQ0 at PIC */
  pic_clear_mask(0);

  serial_print("[PIT] Initialized at 1000 Hz (Mode 3)\n");
}

/* Get current tick count */
uint64_t timer_get_ticks(void) {
  /* two 32-bit loads: keep IRQ0 out of the middle */
  uint32_t flags = irq_save();
  uint64_t ticks = global_ticks;
  irq_restore(flags);
  return ticks;
}
