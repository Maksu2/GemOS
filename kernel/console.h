#ifndef CONSOLE_H
#define CONSOLE_H

#include "../include/gemos/console_abi.h"
#include <gemos/window_abi.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void console_init(void);
int console_open(uint32_t owner_pid, const char *title, uint32_t cols,
                 uint32_t rows, uint32_t flags);
int console_write(uint32_t owner_pid, int handle, const char *buffer,
                  size_t length);
int console_poll_event(uint32_t owner_pid, int handle,
                       gemos_console_event_t *event);
int console_clear(uint32_t owner_pid, int handle);
int console_present(uint32_t owner_pid, int handle,
                    const gemos_console_frame_t *frame,
                    const gemos_console_cell_t *cells);
void console_destroy_for_pid(uint32_t owner_pid);
int console_open_window(uint32_t owner_pid, const char *title,
                        uint32_t width, uint32_t height, uint32_t icon);
int console_present_window(uint32_t owner_pid, int handle,
                           const gemos_draw_command_t *commands, uint32_t count);
int console_window_menu(uint32_t owner_pid, int handle, const gemos_window_menu_t *menu);

#ifdef GEMOS_SELFTEST
bool console_test_contains(uint32_t owner_pid, const char *text);
int console_test_event(uint32_t owner_pid, gemos_console_event_t event);
#endif

#endif /* CONSOLE_H */
