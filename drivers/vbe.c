/*
 * GemOS VBE / BGA Driver
 *
 * Provides access to the linear framebuffer for graphics output.
 * Supports BGA (Bochs Graphics Adapter) page flipping for tear-free rendering.
 *
 * BGA registers are accessed via I/O ports 0x01CE (index) / 0x01CF (data).
 * Page flipping works by changing the Y_OFFSET register to point to
 * a different page in VRAM. This is instantaneous and atomic.
 */

#include "vbe.h"
#include <io.h>

/* Framebuffer state */
static uint32_t *fb_addr = 0;
static uint16_t fb_width = 0;
static uint16_t fb_height = 0;
static uint8_t fb_bpp = 0;
static uint16_t fb_pitch = 0;

/* BGA Page Flipping */
#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT 0x01CF
#define BGA_REG_Y_OFFSET 0x0009
#define BGA_REG_VIDEO_MEMORY_64K 0x000A

static uint32_t page_size = 0;   /* Bytes per page */
static int current_page = 0;     /* Currently displayed page (0 or 1) */
static int bga_flip_enabled = 0; /* Whether page flipping is available */

/* BGA register access */
static void bga_write(uint16_t reg, uint16_t val) {
  outw(BGA_INDEX_PORT, reg);
  outw(BGA_DATA_PORT, val);
}

static uint16_t bga_read(uint16_t reg) {
  outw(BGA_INDEX_PORT, reg);
  return inw(BGA_DATA_PORT);
}

/* Initialize VBE driver */
void vbe_init(uint32_t framebuffer_addr, uint16_t width, uint16_t height,
              uint8_t bpp, uint16_t pitch) {
  fb_addr = (uint32_t *)(uintptr_t)framebuffer_addr;
  fb_width = width;
  fb_height = height;
  fb_bpp = bpp;
  fb_pitch = pitch;

  page_size = (uint32_t)pitch * height;

  /* Check if BGA has enough VRAM for 2 pages */
  uint16_t vram_64k = bga_read(BGA_REG_VIDEO_MEMORY_64K);
  uint32_t vram_total = (uint32_t)vram_64k * 65536;

  if (vram_total >= page_size * 2) {
    bga_flip_enabled = 1;
    current_page = 0;
  } else {
    bga_flip_enabled = 0;
  }
}

/* Get pointer to the back page (the one we render to) */
uint32_t *vbe_get_back_page(void) {
  if (!bga_flip_enabled)
    return fb_addr;

  /* Back page is the opposite of current_page */
  int back = 1 - current_page;
  return (uint32_t *)((uint8_t *)fb_addr + back * page_size);
}

/* Flip: make the back page visible, swap roles */
void vbe_flip(void) {
  if (!bga_flip_enabled)
    return;

  /* The back page (which we just rendered to) becomes the front */
  int back = 1 - current_page;
  bga_write(BGA_REG_Y_OFFSET, (uint16_t)(back * fb_height));
  current_page = back;
}

int vbe_has_page_flip(void) { return bga_flip_enabled; }

/* Getters */
uint16_t vbe_get_width(void) { return fb_width; }
uint16_t vbe_get_height(void) { return fb_height; }
uint8_t vbe_get_bpp(void) { return fb_bpp; }
uint16_t vbe_get_pitch(void) { return fb_pitch; }
uint32_t *vbe_get_framebuffer(void) { return fb_addr; }
