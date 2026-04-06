#include "pit.h"
#include "../include/io.h"
#include "../kernel/include/event.h"
#include "../kernel/isr.h"
#include "pic.h"
#include "serial.h"

/* Global tick counter */
static volatile uint64_t global_ticks = 0;
static uint32_t log_counter = 0;

/* Core timer logic — called from scheduler_tick (bypasses isr_handler path) */
void pit_tick(void) {
  global_ticks++;
  log_counter++;

  /* Push Timer Event every 10 ticks (10ms) to avoid spamming the queue */
  if (global_ticks % 10 == 0) {
    event_t ev;
    ev.type = EVENT_TIMER_TICK;
    ev.data.timer.tick_count = global_ticks;
    event_push(ev);
  }

  /* Log every 1000 ticks (1 second) */
  if (log_counter >= 1000) {
    log_counter = 0;
    serial_print("[TIMER] 1 second passed (Ticks: ");
    serial_print_dec((uint32_t)global_ticks);
    serial_print(")\n");
  }
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
uint64_t timer_get_ticks(void) { return global_ticks; }
