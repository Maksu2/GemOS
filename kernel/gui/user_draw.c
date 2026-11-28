#include "user_draw.h"
#include "../gfx/font/font.h"
#include "../gfx/icons.h"
#include "../gfx/primitives.h"
#include "../ui/theme.h"

const icon_t *user_draw_icon(uint32_t id) {
  switch (id) {
  case GEMOS_ICON_FOLDER: return &icon_folder;
  case GEMOS_ICON_TEXT: return &icon_textfile;
  case GEMOS_ICON_PROGRAM: return &icon_terminal;
  case GEMOS_ICON_FILE: return &icon_generic_file;
  default: return 0;
  }
}

bool user_draw_valid(const gemos_draw_command_t *commands, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    const gemos_draw_command_t *cmd = &commands[i];
    if (cmd->x < 0 || cmd->y < 0 || cmd->w < 0 || cmd->h < 0 ||
        (uint32_t)cmd->x > GEMOS_WINDOW_LIMIT || (uint32_t)cmd->y > GEMOS_WINDOW_LIMIT ||
        (uint32_t)cmd->w > GEMOS_WINDOW_LIMIT || (uint32_t)cmd->h > GEMOS_WINDOW_LIMIT ||
        cmd->color > 0xffffffU) {
      return false;
    }
    switch (cmd->kind) {
    case GEMOS_DRAW_FILL:
    case GEMOS_DRAW_FRAME:
      if (!cmd->w || !cmd->h) return false;
      break;
    case GEMOS_DRAW_TEXT:
      if (!cmd->w || cmd->value < 10 || cmd->value > 24 ||
          cmd->text[GEMOS_DRAW_TEXT_BYTES - 1] != '\0') return false;
      break;
    case GEMOS_DRAW_ICON:
      if (!user_draw_icon(cmd->value)) return false;
      break;
    default:
      return false;
    }
  }
  return true;
}

/* WM sets the client clip before calling this. The retained commands are
 * in logical pixels, so moving a window never reads the process's memory. */
void user_draw_render(window_t *win, const gemos_draw_command_t *commands,
                      uint32_t count) {
  gfx_context_t *ctx = &win->ctx;
  rect_t client = win->client_rect;
  gfx_fill_rect(ctx, client.x, client.y, client.w, client.h, UI_PAPER);
  for (uint32_t i = 0; i < count; ++i) {
    const gemos_draw_command_t *cmd = &commands[i];
    int x = client.x + cmd->x;
    int y = client.y + cmd->y;
    switch (cmd->kind) {
    case GEMOS_DRAW_FILL:
      gfx_fill_rect(ctx, x, y, cmd->w, cmd->h, cmd->color);
      break;
    case GEMOS_DRAW_FRAME:
      gfx_draw_rect(ctx, x, y, cmd->w, cmd->h, cmd->color);
      break;
    case GEMOS_DRAW_TEXT:
      font_draw_text_fit(ctx, x, y, cmd->text, (int)cmd->value, cmd->color, cmd->w);
      break;
    case GEMOS_DRAW_ICON:
      icon_draw_outline(ctx, x, y, user_draw_icon(cmd->value), cmd->color);
      break;
    }
  }
}
