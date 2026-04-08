#include "../common/hosted_app.h"
#include "term_commands.h"
#include "term_input.h"
#include "term_model.h"
#include "term_render.h"
#include "term_theme.h"

#include <stddef.h>
#include <stdint.h>

static gemos_hosted_app_t app_host;
static term_model_t app_model;
static char app_command[UTERM2_INPUT_MAX];
static gemos_console_cell_t app_frame_cells[UTERM2_ROWS * UTERM2_COLS];

int main(void) {
  int32_t poll_result;

  if (gemos_hosted_app_open(&app_host, UTERM2_TITLE, UTERM2_COLS, UTERM2_ROWS,
                            0U) < 0) {
    static const char open_failed[] = "UTERM2 failed to open console\n";
    gemos_debug_write(open_failed, sizeof(open_failed) - 1U);
    return 1;
  }

  term_model_init(&app_model);
  term_model_append_text(&app_model, UTERM2_HINT_STYLE,
                         "Type 'help' to list commands.");

  for (;;) {
    int saw_event = 0;

    while ((poll_result = gemos_hosted_app_poll_event(&app_host)) == 1) {
      saw_event = 1;
      if (term_input_handle_event(&app_model, &app_host.event, app_command,
                                  sizeof(app_command)) ==
          TERM_INPUT_SUBMIT) {
        term_model_append_command(&app_model, app_command);
        term_commands_execute(&app_model, app_command);
      }
    }

    if (poll_result < 0) {
      return 2;
    }

    if (app_model.dirty) {
      term_render_build_frame(&app_model, app_host.pid, &app_host.frame,
                              app_frame_cells);
      if (gemos_hosted_app_present(&app_host, app_frame_cells) < 0) {
        return 3;
      }
      term_model_mark_clean(&app_model);
    }

    if (app_model.should_exit) {
      return 0;
    }

    if (!saw_event && !app_model.dirty) {
      gemos_hosted_app_idle();
    }
  }
}
