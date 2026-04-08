#include "about_state.h"

void about_state_init(about_state_t *state, uint32_t pid) {
  if (state == 0) {
    return;
  }

  state->pid = pid;
  state->uptime_seconds = 0;
  state->dirty = 1;
  state->should_exit = 0;
}

void about_state_handle_event(about_state_t *state,
                              const gemos_console_event_t *event) {
  uint32_t ch;

  if (state == 0 || event == 0) {
    return;
  }
  if (event->type == GEMOS_CONSOLE_EVENT_CLOSE_REQUEST) {
    state->should_exit = 1;
    state->dirty = 1;
    return;
  }
  if (event->type != GEMOS_CONSOLE_EVENT_KEY) {
    return;
  }

  ch = event->character;
  if (ch == GEMOS_KEY_ESC || ch == 'q' || ch == 'Q') {
    state->should_exit = 1;
    state->dirty = 1;
  }
}

void about_state_tick(about_state_t *state, uint32_t now_ms) {
  uint32_t new_seconds;

  if (state == 0) {
    return;
  }

  new_seconds = now_ms / 1000U;
  if (new_seconds != state->uptime_seconds) {
    state->uptime_seconds = new_seconds;
    state->dirty = 1;
  }
}

void about_state_mark_clean(about_state_t *state) {
  if (state != 0) {
    state->dirty = 0;
  }
}
