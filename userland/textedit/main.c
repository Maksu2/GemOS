#include "../common/hosted_app.h"
#include "textedit_render.h"
#include "textedit_state.h"
#include "textedit_theme.h"

static gemos_hosted_app_t textedit_app;
static utextedit_state_t textedit_state;
static gemos_console_cell_t
    textedit_cells[UTEXTEDIT_ROWS * UTEXTEDIT_COLS];

int main(void) {
  int32_t poll_result;

  if (gemos_hosted_app_open(&textedit_app, UTEXTEDIT_TITLE, UTEXTEDIT_COLS,
                            UTEXTEDIT_ROWS, 0U) < 0) {
    static const char open_failed[] = "UTEXTEDIT failed to open console\n";
    gemos_debug_write(open_failed, sizeof(open_failed) - 1U);
    return 1;
  }

  utextedit_state_init(&textedit_state);

  for (;;) {
    int saw_event = 0;

    while ((poll_result = gemos_hosted_app_poll_event(&textedit_app)) == 1) {
      saw_event = 1;
      utextedit_state_handle_event(&textedit_state, &textedit_app.event);
    }

    if (poll_result < 0) {
      return 2;
    }

    if (textedit_state.dirty) {
      utextedit_render_build_frame(&textedit_state, &textedit_app.frame,
                                   textedit_cells);
      if (gemos_hosted_app_present(&textedit_app, textedit_cells) < 0) {
        return 3;
      }
      utextedit_state_mark_clean(&textedit_state);
    }

    if (textedit_state.should_exit) {
      return 0;
    }

    if (!saw_event && !textedit_state.dirty) {
      gemos_hosted_app_idle();
    }
  }
}
