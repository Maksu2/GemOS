#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

typedef enum {
  EVENT_NONE = 0,
  EVENT_KEY_PRESS,
  EVENT_KEY_RELEASE,
  EVENT_MOUSE_MOVE,
  EVENT_MOUSE_CLICK,
  EVENT_MOUSE_RELEASE,
  EVENT_TIMER_TICK,
  EVENT_REDRAW_REQUEST
} event_type_t;

typedef struct {
  event_type_t type;
  union {
    struct {
      uint8_t key_code;
      char character;
      uint32_t modifiers;
    } key;
    struct {
      int32_t x;
      int32_t y;
      uint8_t buttons;
      int32_t dx;
      int32_t dy;
    } mouse;
    struct {
      uint64_t tick_count;
    } timer;
  } data;
} event_t;

/* Initialize event system */
void event_init(void);

/* Push an event to the queue (Safe to call from ISR) */
int event_push(event_t event);

/* Pop an event from the queue (Returns 1 if event popped, 0 if empty) */
int event_pop(event_t *event);

#endif /* EVENT_H */
