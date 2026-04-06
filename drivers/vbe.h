/*
 * GemOS VBE (VESA BIOS Extensions) Driver
 *
 * Provides access to the linear framebuffer for graphics output.
 */

#ifndef VBE_H
#define VBE_H

#include <stdint.h>

/* VBE Mode Info Block */
typedef struct vbe_mode_info {
  uint16_t attributes;
  uint8_t window_a;
  uint8_t window_b;
  uint16_t granularity;
  uint16_t window_size;
  uint16_t segment_a;
  uint16_t segment_b;
  uint32_t win_func_ptr;
  uint16_t pitch;
  uint16_t width;
  uint16_t height;
  uint8_t w_char;
  uint8_t y_char;
  uint8_t planes;
  uint8_t bpp;
  uint8_t banks;
  uint8_t memory_model;
  uint8_t bank_size;
  uint8_t image_pages;
  uint8_t reserved0;

  uint8_t red_mask;
  uint8_t red_position;
  uint8_t green_mask;
  uint8_t green_position;
  uint8_t blue_mask;
  uint8_t blue_position;
  uint8_t rsv_mask;
  uint8_t rsv_position;
  uint8_t direct_color_attributes;

  uint32_t physbase;
  uint32_t reserved1;
  uint16_t reserved2;
} __attribute__((packed)) vbe_mode_info_t;

/* Initialize VBE driver with framebuffer info */
void vbe_init(uint32_t framebuffer_addr, uint16_t width, uint16_t height,
              uint8_t bpp, uint16_t pitch);

/* Get framebuffer dimensions */
uint16_t vbe_get_width(void);
uint16_t vbe_get_height(void);
uint8_t vbe_get_bpp(void);
uint16_t vbe_get_pitch(void);

/* Direct framebuffer access */
uint32_t *vbe_get_framebuffer(void);

/* BGA Page Flipping */
uint32_t *vbe_get_back_page(void); /* Get render target (invisible page) */
void vbe_flip(void);               /* Swap visible/invisible pages */
int vbe_has_page_flip(void);       /* Check if page flip is available */

#endif /* VBE_H */
