#include "../common/hosted_app.h"
#include "textedit_render.h"
#include "textedit_state.h"
#include "textedit_theme.h"

static gemos_hosted_app_t textedit_app;
static utextedit_state_t textedit_state;
static gemos_console_cell_t
    textedit_cells[UTEXTEDIT_ROWS * UTEXTEDIT_COLS];

/* argv[1], when given, is the file to open (the File Explorer passes it). */
int main(int argc, char **argv) {
  int32_t poll_result;

  if (gemos_hosted_app_open(&textedit_app, UTEXTEDIT_TITLE, UTEXTEDIT_COLS,
                            UTEXTEDIT_ROWS, 0U) < 0) {
    static const char open_failed[] = "UTEXTEDIT failed to open console\n";
    gemos_debug_write(open_failed, sizeof(open_failed) - 1U);
    return 1;
  }

  utextedit_state_init(&textedit_state);
  if (argc > 1) {
    utextedit_state_open_start_file(&textedit_state, argv[1]);
  }

  for (;;) {
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

    /* Sleep until the next event, then handle everything queued. */
    poll_result =
        gemos_hosted_app_wait_event(&textedit_app, GEMOS_WAIT_FOREVER);
    while (poll_result == 1) {
      utextedit_state_handle_event(&textedit_state, &textedit_app.event);
      poll_result = gemos_hosted_app_poll_event(&textedit_app);
    }

    if (poll_result < 0) {
      return 2;
    }
  }
}
