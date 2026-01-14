#include "video.h"
#include "font.h"

// VESA Info from Bootloader
typedef struct {
  uint32_t framebuffer_addr;
  uint16_t width;
  uint16_t height;
  uint8_t bpp;
  uint16_t pitch;
} __attribute__((packed)) VesaInfo;

#define VESA_INFO_LOC 0x9000
// Backbuffer location: 8MB mark. Ensure QEMU has -m 32 or more.
#define BACKBUFFER_ADDR 0x800000

VesaInfo *vesa_info = (VesaInfo *)VESA_INFO_LOC;
uint8_t *framebuffer;
uint8_t *backbuffer;

int screen_width = 1024; // Default safe
int screen_height = 768;

void init_video() {
  framebuffer = (uint8_t *)vesa_info->framebuffer_addr;
  backbuffer = (uint8_t *)BACKBUFFER_ADDR;
  screen_width = vesa_info->width;
  screen_height = vesa_info->height;
}

// Draw to BACKBUFFER
void put_pixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= vesa_info->width || y < 0 || y >= vesa_info->height)
    return;

  // Calculate offset
  // Because we are double buffering, we write to backbuffer (always 32bpp
  // format internally in our simple OS?) Actually, if backbuffer is just a
  // chunk of memory, we should treat it as 32bpp for convenience and then
  // convert during swap? Or just mirror VRAM format? Let's mirror VRAM format
  // for simplicity in swap.

  uint32_t offset = y * vesa_info->pitch + x * (vesa_info->bpp / 8);

  if (vesa_info->bpp == 32) {
    // Fast path
    *(uint32_t *)(backbuffer + offset) = color;
  } else {
    // 24 bpp
    backbuffer[offset] = color & 0xFF;             // Blue
    backbuffer[offset + 1] = (color >> 8) & 0xFF;  // Green
    backbuffer[offset + 2] = (color >> 16) & 0xFF; // Red
  }
}

// Read from BACKBUFFER (fast read)
uint32_t get_pixel(int x, int y) {
  if (x < 0 || x >= vesa_info->width || y < 0 || y >= vesa_info->height)
    return 0;

  uint32_t offset = y * vesa_info->pitch + x * (vesa_info->bpp / 8);

  if (vesa_info->bpp == 32) {
    return *(uint32_t *)(backbuffer + offset);
  } else {
    uint8_t b = backbuffer[offset];
    uint8_t g = backbuffer[offset + 1];
    uint8_t r = backbuffer[offset + 2];
    return (r << 16) | (g << 8) | b;
  }
}

// Swap buffers (Copy Backbuffer to Framebuffer)
// Dithering helper
// Checkerboard pattern: (x+y)%2 == 0 ? color1 : color2
void video_clear_dithered(uint32_t c1, uint32_t c2) {
  int bpp = vesa_info->bpp / 8;
  int pitch = vesa_info->pitch;
  int w = vesa_info->width;
  int h = vesa_info->height;

  for (int y = 0; y < h; y++) {
    uint8_t *row = (uint8_t *)backbuffer + y * pitch;
    for (int x = 0; x < w; x++) {
      uint32_t color = ((x + y) & 1) ? c1 : c2;

      if (bpp == 4) {
        *(uint32_t *)row = color;
        row += 4;
      } else {
        *row++ = color & 0xFF;
        *row++ = (color >> 8) & 0xFF;
        *row++ = (color >> 16) & 0xFF;
      }
    }
  }
}

void video_swap() {
  // Copy backbuffer to framebuffer
  // Size = height * pitch
  // Basic memcpy
  // Assuming pitch is bytes per line.

  uint32_t total_bytes = vesa_info->height * vesa_info->pitch;
  uint8_t *dst = (uint8_t *)framebuffer;
  uint8_t *src = backbuffer;

  // We can write a fast 32-bit copy loop even if size is large
  // but byte copy is safest if unaligned (unlikely for pitch).
  for (uint32_t i = 0; i < total_bytes; i++) {
    dst[i] = src[i];
  }
}

// Clear backbuffer
void video_clear(uint32_t color) {
  uint32_t total_bytes = vesa_info->height * vesa_info->pitch;

  // IF 32bpp, fast fill?
  // But 'color' is 32-bit.
  // Loop pixel by pixel or just memset if color is uniform bytes (0 or
  // FFFFFFFF). If color is specific (e.g. 0x008080), bytes are different.

  // Fallback to rect or pixel loop for correctness
  // draw_rect(0, 0, vesa_info->width, vesa_info->height, color);
  // Actually we can just run per-byte loop if careful? No.
  // Let's iterate pixels.

  // Optimized pixel loop
  int bpp = vesa_info->bpp / 8;
  int w = vesa_info->width;
  int h = vesa_info->height;
  int pitch = vesa_info->pitch;

  for (int y = 0; y < h; y++) {
    uint8_t *row = backbuffer + y * pitch;
    for (int x = 0; x < w; x++) {
      if (bpp == 4) {
        *(uint32_t *)row = color;
        row += 4;
      } else {
        *row++ = color & 0xFF;
        *row++ = (color >> 8) & 0xFF;
        *row++ = (color >> 16) & 0xFF;
      }
    }
  }
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
  for (int i = 0; i < h; i++) {
    for (int j = 0; j < w; j++) {
      put_pixel(x + j, y + i, color);
    }
  }
}

void draw_char(int x, int y, char c, uint32_t color) {
  if (c < 32 || c > 126)
    return;
  int index = c - 32;

  for (int row = 0; row < 8; row++) {
    uint8_t line = font_basic[index][row];
    for (int col = 0; col < 8; col++) {
      if (line & (0x80 >> col)) {
        put_pixel(x + col, y + row, color);
      }
    }
  }
}

void draw_string(int x, int y, const char *str, uint32_t color) {
  int cur_x = x;
  while (*str) {
    draw_char(cur_x, y, *str, color);
    cur_x += 8;
    str++;
  }
}
