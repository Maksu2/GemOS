#include "../gfx/primitives.h"
#include "font.h"

/*
   Grayscale Anti-Aliasing Blending
   Mixes foreground color with background buffer based on alpha (0-255).
*/

/* sRGB to Linear LUT (Gamma 2.2) */
static const uint8_t srgb_to_linear[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   2,
    2,   2,   2,   2,   3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,
    5,   5,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,   9,   10,
    10,  10,  11,  11,  12,  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,
    17,  17,  18,  18,  19,  19,  20,  21,  21,  22,  22,  23,  23,  24,  25,
    25,  26,  27,  27,  28,  29,  29,  30,  31,  31,  32,  33,  33,  34,  35,
    36,  36,  37,  38,  39,  40,  40,  41,  42,  43,  44,  45,  45,  46,  47,
    48,  49,  50,  51,  52,  53,  54,  55,  55,  56,  57,  58,  59,  60,  61,
    62,  63,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  77,  78,
    79,  80,  81,  82,  84,  85,  86,  87,  88,  90,  91,  92,  93,  95,  96,
    97,  99,  100, 101, 103, 104, 105, 107, 108, 109, 111, 112, 114, 115, 117,
    118, 119, 121, 122, 124, 125, 127, 128, 130, 131, 133, 135, 136, 138, 139,
    141, 142, 144, 146, 147, 149, 151, 152, 154, 156, 157, 159, 161, 162, 164,
    166, 168, 169, 171, 173, 175, 176, 178, 180, 182, 184, 186, 187, 189, 191,
    193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 233, 235, 237, 239, 241, 244, 246, 248, 250, 252,
    255};

/* Linear to sRGB LUT (Inverse Gamma 1/2.2) */
static const uint8_t linear_to_srgb[256] = {
    0,   20,  28,  33,  38,  42,  46,  49,  52,  55,  58,  61,  63,  65,  68,
    70,  72,  74,  76,  78,  80,  81,  83,  85,  87,  88,  90,  91,  93,  94,
    96,  97,  99,  100, 102, 103, 104, 106, 107, 108, 109, 111, 112, 113, 114,
    115, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 128, 129, 130, 131,
    132, 133, 134, 135, 136, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 147, 148, 149, 150, 151, 152, 153, 153, 154, 155, 156, 157, 158,
    158, 159, 160, 161, 162, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169,
    170, 171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 178, 179, 180,
    181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189, 190,
    190, 191, 192, 192, 193, 194, 194, 195, 196, 196, 197, 197, 198, 199, 199,
    200, 200, 201, 202, 202, 203, 203, 204, 205, 205, 206, 206, 207, 208, 208,
    209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 216, 216, 217,
    217, 218, 218, 219, 219, 220, 220, 221, 222, 222, 223, 223, 224, 224, 225,
    225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232,
    233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 240,
    240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 247, 247,
    248, 248, 249, 249, 249, 250, 250, 251, 251, 252, 252, 253, 253, 254, 254,
    255};

static inline uint8_t mix_channel_gamma(uint8_t bg, uint8_t fg, uint8_t a) {
  uint32_t bg_lin = srgb_to_linear[bg];
  uint32_t fg_lin = srgb_to_linear[fg];
  /* Blend in linear space */
  /* out = (bg * (255 - a) + fg * a) / 255 */
  uint32_t out_lin = (bg_lin * (255 - a) + fg_lin * a) / 255;
  if (out_lin > 255)
    out_lin = 255;
  return linear_to_srgb[out_lin];
}

void aa_blend_pixel(gfx_context_t *ctx, int x, int y, uint32_t color,
                    uint8_t alpha) {
  /* Clipping */
  if (x < 0 || y < 0 || (uint32_t)x >= ctx->width || (uint32_t)y >= ctx->height)
    return;

  /* Optimized cases */
  if (alpha == 0)
    return;

  /* Calculate pixel address using pitch to be safe */
  uint8_t *row_ptr = (uint8_t *)ctx->framebuffer + (y * ctx->pitch);

  if (ctx->bpp == 32) {
    uint32_t *pixel_ptr = (uint32_t *)(row_ptr + x * 4);

    uint32_t bg = *pixel_ptr;
    uint8_t r_bg = (bg >> 16) & 0xFF;
    uint8_t g_bg = (bg >> 8) & 0xFF;
    uint8_t b_bg = (bg) & 0xFF;

    uint8_t r_fg = (color >> 16) & 0xFF;
    uint8_t g_fg = (color >> 8) & 0xFF;
    uint8_t b_fg = (color) & 0xFF;

    uint8_t r_out = mix_channel_gamma(r_bg, r_fg, alpha);
    uint8_t g_out = mix_channel_gamma(g_bg, g_fg, alpha);
    uint8_t b_out = mix_channel_gamma(b_bg, b_fg, alpha);

    *pixel_ptr = (r_out << 16) | (g_out << 8) | b_out;

  } else if (ctx->bpp == 24) {
    uint8_t *pixel_ptr = row_ptr + x * 3;

    uint8_t b_bg = pixel_ptr[0];
    uint8_t g_bg = pixel_ptr[1];
    uint8_t r_bg = pixel_ptr[2];

    uint8_t r_fg = (color >> 16) & 0xFF;
    uint8_t g_fg = (color >> 8) & 0xFF;
    uint8_t b_fg = (color) & 0xFF;

    uint8_t r_out = mix_channel_gamma(r_bg, r_fg, alpha);
    uint8_t g_out = mix_channel_gamma(g_bg, g_fg, alpha);
    uint8_t b_out = mix_channel_gamma(b_bg, b_fg, alpha);

    pixel_ptr[0] = b_out;
    pixel_ptr[1] = g_out;
    pixel_ptr[2] = r_out;
  }
}
