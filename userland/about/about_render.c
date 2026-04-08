#include "about_render.h"

#include "../common/format.h"
#include "../common/text_surface.h"
#include "about_theme.h"

#include <stddef.h>
#include <stdint.h>

static void about_render_field(gemos_console_cell_t *cells, uint32_t row,
                               const char *label, const char *value) {
  gemos_text_surface_write_label_value(
      cells, ABOUT_COLS, ABOUT_ROWS, row, ABOUT_LABEL_COL, ABOUT_VALUE_COL,
      ABOUT_DIM_STYLE, ABOUT_TEXT_STYLE, label, value);
}

void about_render_build_frame(const about_state_t *state,
                              gemos_console_frame_t *frame,
                              gemos_console_cell_t *cells) {
  char pid_text[16];
  char uptime_text[32];

  if (state == 0 || frame == 0 || cells == 0) {
    return;
  }

  gemos_text_surface_clear(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_TEXT_STYLE);
  gemos_text_surface_fill_row(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_HEADER_ROW,
                              ABOUT_STATUS_STYLE, ' ');
  gemos_text_surface_write_text(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_HEADER_ROW,
                                ABOUT_CONTENT_COL, ABOUT_STATUS_STYLE,
                                ABOUT_TITLE);
  gemos_text_surface_write_right(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_HEADER_ROW,
                                 ABOUT_CONTENT_COL, ABOUT_STATUS_STYLE,
                                 "Esc/Q close");

  gemos_text_surface_write_centered(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_HERO_ROW,
                                    ABOUT_ACCENT_STYLE, "GemOS");
  gemos_text_surface_write_centered(cells, ABOUT_COLS, ABOUT_ROWS,
                                    ABOUT_SUBTITLE_ROW,
                                    ABOUT_DIM_STYLE,
                                    "boring 32-bit desktop operating system");

  gemos_text_surface_write_text(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_SECTION_ROW,
                                ABOUT_CONTENT_COL,
                                ABOUT_DIM_STYLE, "System");
  gemos_text_surface_write_hline(cells, ABOUT_COLS, ABOUT_ROWS,
                                 ABOUT_SECTION_ROW + 1U, ABOUT_CONTENT_COL,
                                 ABOUT_COLS - (ABOUT_CONTENT_COL * 2U),
                                 ABOUT_DIM_STYLE, '-');

  gemos_u32_to_text(state->pid, pid_text, sizeof(pid_text));
  gemos_format_uptime(state->uptime_seconds, uptime_text, sizeof(uptime_text));

  about_render_field(cells, ABOUT_FIELDS_ROW + 0U, "Version", ABOUT_VERSION_LABEL);
  about_render_field(cells, ABOUT_FIELDS_ROW + 1U, "Build", ABOUT_BUILD_LABEL);
  about_render_field(cells, ABOUT_FIELDS_ROW + 2U, "PID", pid_text);
  about_render_field(cells, ABOUT_FIELDS_ROW + 3U, "Uptime", uptime_text);
  about_render_field(cells, ABOUT_FIELDS_ROW + 4U, "Mode",
                     "Ring 3 | Hosted Surface");

  gemos_text_surface_write_centered(cells, ABOUT_COLS, ABOUT_ROWS,
                                    ABOUT_FOOTER_ROW - 1U,
                                    ABOUT_DIM_STYLE, "Boring is Success");
  gemos_text_surface_fill_row(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_FOOTER_ROW,
                              ABOUT_DIM_STYLE, ' ');
  gemos_text_surface_write_text(cells, ABOUT_COLS, ABOUT_ROWS, ABOUT_FOOTER_ROW,
                                ABOUT_CONTENT_COL, ABOUT_DIM_STYLE,
                                "Hosted app");
  gemos_text_surface_write_right(cells, ABOUT_COLS, ABOUT_ROWS,
                                 ABOUT_FOOTER_ROW, ABOUT_CONTENT_COL,
                                 ABOUT_DIM_STYLE, "Ring 3");

  gemos_text_surface_write_centered(cells, ABOUT_COLS, ABOUT_ROWS,
                                    ABOUT_FOOTER_ROW - 2U,
                                    ABOUT_DIM_STYLE,
                                    "Window hosting stays in kernel space");

  frame->cols = ABOUT_COLS;
  frame->rows = ABOUT_ROWS;
  frame->cursor_row = 0U;
  frame->cursor_col = 0U;
  frame->cursor_visible = 0U;
  frame->cells = cells;
}
