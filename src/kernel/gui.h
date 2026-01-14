#ifndef GUI_H
#define GUI_H

#include "../kernel/types.h"

void draw_window(int x, int y, int w, int h, const char *title);
void update_mouse_cursor(int x, int y);
void init_gui();

// Mouse State
extern int mouse_x;
extern int mouse_y;

#endif
