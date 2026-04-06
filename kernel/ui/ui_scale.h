#ifndef UI_SCALE_H
#define UI_SCALE_H

/* Global UI Scale Factor */
/* Default should be 1.0, but we set to user request of 2.0 for FHD */
extern float ui_scale;

void ui_init_scale(float scale);

#endif
