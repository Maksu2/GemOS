#include "include/event.h"
#include "../drivers/serial.h"

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
  return 1;
}

int event_pop(event_t *event) {
  if (head == tail) {
    return 0; /* Empty */
  }

  *event = event_queue[tail];
  tail = (tail + 1) % MAX_EVENTS;
  return 1;
}
