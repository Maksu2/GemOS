#include "focus.h"
#include "../../drivers/serial.h"
#include "../gui/topbar/topbar.h"

/* Forward declare TopBar handler or include header */
/* topbar.h likely doesn't have it yet. */
extern void topbar_on_focus_changed(focus_state_t *state);

void ui_on_focus_changed(focus_state_t *state) {
  if (!state)
    return;

  serial_print("[FOCUS] Dispatching event. Type: ");
  if (state->type == FOCUS_DESKTOP)
    serial_print("DESKTOP\n");
  else
    serial_print("WINDOW\n");

  topbar_on_focus_changed(state);

  /* Future: dock_on_focus_changed(state); */
}
