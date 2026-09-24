#ifndef UI_SCALE_H
#define UI_SCALE_H

/* Global UI scale factor: physical pixels per logical pixel (2 on a
 * 1920x1080 screen, 1 on the smaller modes the loader falls back to).
 * An integer, so code in interrupt handlers (mouse) needs no FPU. */
extern int ui_scale;

#endif
