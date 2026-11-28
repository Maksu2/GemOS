#ifndef USER_DRAW_H
#define USER_DRAW_H

#include "window/window.h"
#include <gemos/window_abi.h>

const icon_t *user_draw_icon(uint32_t id);
bool user_draw_valid(const gemos_draw_command_t *commands, uint32_t count);
void user_draw_render(window_t *win, const gemos_draw_command_t *commands,
                      uint32_t count);

#endif
