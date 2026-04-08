#ifndef GEMOS_USERLAND_ABOUT_STATE_H
#define GEMOS_USERLAND_ABOUT_STATE_H

#include <gemos/console_abi.h>

#include <stdint.h>

typedef struct {
  uint32_t pid;
  uint32_t uptime_seconds;
  uint8_t dirty;
  uint8_t should_exit;
} about_state_t;

void about_state_init(about_state_t *state, uint32_t pid);
void about_state_handle_event(about_state_t *state,
                              const gemos_console_event_t *event);
void about_state_tick(about_state_t *state, uint32_t now_ms);
void about_state_mark_clean(about_state_t *state);

#endif /* GEMOS_USERLAND_ABOUT_STATE_H */
