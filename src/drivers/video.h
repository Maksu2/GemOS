#ifndef VIDEO_H
#define VIDEO_H

#include "../kernel/types.h"

void init_video();
void put_pixel(int x, int y, uint32_t color);
uint32_t get_pixel(int x, int y);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void video_swap();
void video_clear(uint32_t color);
void video_clear_dithered(uint32_t c1, uint32_t c2); // Checkerboard pattern
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color);

extern int screen_width;
extern int screen_height;

#endif
