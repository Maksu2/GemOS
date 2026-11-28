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

enum {
  MOUSE_BUTTON_LEFT = 1U,
  MOUSE_BUTTON_RIGHT = 2U,
  MOUSE_BUTTON_MIDDLE = 4U,
};

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
      int32_t dx;
      int32_t dy;
      uint32_t time_ms;
      uint8_t buttons; /* current pressed buttons */
      uint8_t button;  /* the button that changed, or zero for a move */
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

/* Non-zero if the queue holds at least one event */
int event_pending(void);

#endif /* EVENT_H */
