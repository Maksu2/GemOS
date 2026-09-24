#include "include/event.h"
#include "include/irq.h"
#include "scheduler.h"
#include "../drivers/serial.h"

/* Producers are IRQ handlers (keyboard, mouse, PIT), the consumer is the
 * GUI task. */

#define MAX_EVENTS 256

static event_t event_queue[MAX_EVENTS];
static volatile int head = 0;
static volatile int tail = 0;

void event_init(void) {
  head = 0;
  tail = 0;
  serial_print("[EVENT] Event Queue initialized\n");
}

int event_push(event_t event) {
  /* Calculate next head */
  int next_head = (head + 1) % MAX_EVENTS;

  /* Check overflow */
  if (next_head == tail) {
    /* serial_print("[EVENT] Queue Overflow!\n"); */
    return 0; /* Queue full */
  }

  /* Verify bounds (paranoid check) */
  if (next_head < 0 || next_head >= MAX_EVENTS)
    return 0;

  event_queue[head] = event;
  head = next_head;
  scheduler_wake(TASK_GUI);
  return 1;
}

int event_pop(event_t *event) {
  uint32_t flags = irq_save();

  if (head == tail) {
    irq_restore(flags);
    return 0; /* Empty */
  }

  *event = event_queue[tail];
  tail = (tail + 1) % MAX_EVENTS;
  irq_restore(flags);
  return 1;
}

int event_pending(void) { return head != tail; }
