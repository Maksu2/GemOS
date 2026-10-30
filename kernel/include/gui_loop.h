#ifndef GUI_LOOP_H
#define GUI_LOOP_H

/* The GUI loop in kernel_main (kernel/kernel.c): what makes it draw. Call
 * from task context only (never from an interrupt handler). */

/* Something changed that needs the whole screen drawn again: a window
 * opened, closed or moved, a click, a menu. */
void kernel_request_redraw(void);

/* Only this rectangle (logical pixels) shows something new, e.g. a
 * window's client area. The next frame draws just the rectangles
 * reported, unless the whole screen was asked for. */
void kernel_damage(int x, int y, int w, int h);

/* The GemOS menu's Shutdown */
void kernel_request_shutdown(void);

#endif /* GUI_LOOP_H */
