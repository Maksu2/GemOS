#include "glyphs.h"
#include <stddef.h>

/*
   Solid Vector Font Data (Roboto Regular Import)
   Source: Roboto Regular (Google Fonts)
   Grid: 100x100

   Metrics:
   - Cap Height: 20..80 (H=60)
   - x-Height:   38..80 (H=42)
   - Baseline:   80
   - Descender:  100
   - Stroke:     ~13 units
*/

#define PT(x, y) {x, y, false}
#define PT_CLOSE(x, y) {x, y, true}

/* --- Common Shapes --- */
static const font_point_t pts_box[] = {PT(10, 10), PT(90, 10), PT(90, 90),
                                       PT_CLOSE(10, 90)};
static const glyph_t glyph_box = {pts_box, 4, 90};

/* --- Uppercase (H=60, Y=20..80) --- */

// 'G' - Roboto: Spurless, horizontal bar.
static const font_point_t pts_G_rob[] = {
    PT(78, 30),      PT(50, 20), PT(22, 30), PT(22, 70), PT(50, 80), PT(78, 70),
    PT(78, 52),      PT(48, 52), PT(48, 63), PT(66, 63), PT(66, 68), // Throat
    PT(50, 68),      PT(34, 63), PT(34, 37), PT(50, 32), PT(66, 37), PT(78, 30),
    PT_CLOSE(78, 30)};
static const glyph_t glyph_G = {pts_G_rob, 17, 95};

// 'E' - Roboto: Standard, balanced
static const font_point_t pts_E[] = {
    PT(22, 20), PT(78, 20), PT(78, 32),      PT(34, 32), PT(34, 45),
    PT(74, 45), PT(74, 55), PT(34, 55),      PT(34, 68), PT(78, 68),
    PT(78, 80), PT(22, 80), PT_CLOSE(22, 20)};
static const glyph_t glyph_E = {pts_E, 13, 85};

// 'F'
static const font_point_t pts_F[] = {
    PT(22, 20), PT(78, 20), PT(78, 32), PT(34, 32), PT(34, 45),      PT(74, 45),
    PT(74, 55), PT(34, 55), PT(34, 80), PT(22, 80), PT_CLOSE(22, 20)};
static const glyph_t glyph_F = {pts_F, 11, 85};

// 'O' - Roboto: Vertical oval, sides fairly straight
static const font_point_t pts_O[] = {
    // Outer
    PT(30, 20), PT(70, 20), PT(82, 35), PT(82, 65), PT(70, 80), PT(30, 80),
    PT(18, 65), PT(18, 35), PT_CLOSE(30, 20),
    // Inner
    PT(32, 32), PT(32, 68), PT(68, 68), PT(68, 32), PT_CLOSE(68, 32)};
static const glyph_t glyph_O = {pts_O, 14, 98};

// 'S' - Roboto: Open terminals, horizontal cuts
static const font_point_t pts_S[] = {
    PT(72, 22), PT(30, 20), PT(20, 25), PT(20, 46),      PT(60, 48),
    PT(65, 52), PT(65, 70), PT(60, 80), PT(28, 78),      PT(20, 72),
    PT(20, 60), PT(32, 60), PT(32, 68), PT(60, 68),      PT(53, 60),
    PT(35, 52), PT(35, 32), PT(72, 34), PT_CLOSE(72, 22)};
static const glyph_t glyph_S = {pts_S, 18, 92};

// 'A' - Roboto: Flat top
static const font_point_t pts_A[] = {
    /* Outer */
    PT(50, 20), PT(82, 80), PT(68, 80), PT(62, 68), PT(38, 68), PT(32, 80),
    PT(18, 80), PT_CLOSE(50, 20),
    /* Inner */
    PT(50, 32), PT(42, 56), PT(58, 56), PT_CLOSE(58, 32)};
static const glyph_t glyph_A = {pts_A, 12, 100};

// 'V'
static const font_point_t pts_V[] = {PT(14, 20), PT(26, 20),      PT(50, 76),
                                     PT(74, 20), PT(86, 20),      PT(56, 80),
                                     PT(44, 80), PT_CLOSE(14, 20)};
static const glyph_t glyph_V = {pts_V, 8, 96};

// 'I'
static const font_point_t pts_I[] = {PT(44, 20), PT(56, 20), PT(56, 80),
                                     PT(44, 80), PT_CLOSE(44, 20)};
static const glyph_t glyph_I = {pts_I, 5, 55};

// 'L'
static const font_point_t pts_L[] = {PT(22, 20),      PT(34, 20), PT(34, 68),
                                     PT(78, 68),      PT(78, 80), PT(22, 80),
                                     PT_CLOSE(22, 20)};
static const glyph_t glyph_L = {pts_L, 7, 85};

/* --- Lowercase (x-height: 38..80) --- */

// 'e' - Roboto: Horizontal bar, open aperture
static const font_point_t pts_e_clean[] = {
    PT(20, 56), PT(82, 56), PT(82, 48), PT(60, 38), PT(30, 38), PT(16, 48),
    PT(16, 70), PT(30, 80), PT(65, 80), PT(78, 72), PT(78, 62), PT(66, 62),
    PT(66, 68), PT(30, 68), PT(30, 56), PT_CLOSE(20, 56),
    /* Hole */
    PT(30, 48), PT(68, 48), PT(68, 50), PT(30, 50), PT_CLOSE(30, 48)};
static const glyph_t glyph_e = {pts_e_clean, 17, 90};

// 'a' - Roboto: Double story
static const font_point_t pts_a[] = {
    /* Top Arch */
    PT(30, 38), PT(65, 38), PT(78, 38), PT(78, 80), PT(66, 80), PT(66, 66),
    /* Bowl */
    PT(60, 80), PT(30, 80), PT(18, 70), PT(18, 55), PT(30, 50), PT(66, 50),
    PT(66, 45), PT(60, 45), PT(30, 45), PT_CLOSE(30, 38),
    /* Hole */
    PT(30, 60), PT(66, 60), PT(66, 68), PT(30, 68), PT_CLOSE(30, 60)};
static const glyph_t glyph_a = {pts_a, 16, 95};

// 's' - Roboto: Double story, horizontal terminals
static const font_point_t pts_s[] = {
    PT(65, 38), PT(35, 38), PT(25, 42), PT(25, 52), PT(65, 55),
    PT(70, 60), PT(70, 75), PT(65, 80), PT(25, 80), PT(25, 72),
    PT(37, 72), PT(37, 70), PT(60, 70), PT(58, 62), PT(30, 60),
    PT(25, 55), PT(35, 46), PT(35, 46), PT(65, 46), PT_CLOSE(65, 38)};
static const glyph_t glyph_s = {pts_s, 20, 85};

// 'm' - Roboto: Arches
static const font_point_t pts_m[] = {
    PT(10, 38),       PT(22, 38), PT(22, 80),       PT(10, 80),
    PT_CLOSE(10, 38), // Stem
    PT(22, 38),       PT(48, 38), PT(48, 80),       PT(36, 80),
    PT(36, 50),       PT(22, 50), PT_CLOSE(22, 38), // Arch 1
    PT(48, 38),       PT(74, 38), PT(74, 80),       PT(62, 80),
    PT(62, 50),       PT(48, 50), PT_CLOSE(48, 38) // Arch 2
};
static const glyph_t glyph_m = {pts_m, 19, 105}; // Wide

// 'i'
static const font_point_t pts_i[] = {
    PT(44, 20), PT(56, 20),       PT(56, 30),
    PT(44, 30), PT_CLOSE(44, 20), // Round dot logic (square for now)
    PT(44, 38), PT(56, 38),       PT(56, 80),
    PT(44, 80), PT_CLOSE(44, 38)};
static const glyph_t glyph_i = {pts_i, 10, 50};

// 'l'
static const font_point_t pts_l[] = {PT(44, 20), PT(56, 20), PT(56, 80),
                                     PT(44, 80), PT_CLOSE(44, 20)};
static const glyph_t glyph_l = {pts_l, 5, 50};

// 'd'
static const font_point_t pts_d[] = {
    PT(68, 20), PT(80, 20), PT(80, 80), PT(68, 80), PT_CLOSE(68, 20),
    PT(20, 38), PT(68, 38), PT(68, 80), PT(20, 80), PT_CLOSE(20, 38),
    PT(32, 48), PT(32, 70), PT(56, 70), PT(56, 48), PT_CLOSE(56, 48) // Hole
};
static const glyph_t glyph_d = {pts_d, 15, 95};

// 'p'
static const font_point_t pts_p[] = {
    PT(20, 38), PT(32, 38), PT(32, 100), PT(20, 100), PT_CLOSE(20, 38),
    PT(32, 38), PT(80, 38), PT(80, 80),  PT(32, 80),  PT_CLOSE(32, 38),
    PT(44, 48), PT(68, 48), PT(68, 70),  PT(44, 70),  PT_CLOSE(44, 48)};
static const glyph_t glyph_p = {pts_p, 15, 95};

// 't'
static const font_point_t pts_t[] = {
    PT(40, 25), PT(52, 25), PT(52, 38), PT(70, 38),      PT(70, 48), PT(52, 48),
    PT(52, 78), PT(58, 78), PT(60, 75), PT(65, 80),      PT(40, 80), PT(40, 48),
    PT(25, 48), PT(25, 38), PT(40, 38), PT_CLOSE(40, 25)};
static const glyph_t glyph_tee = {pts_t, 16, 70};

// 'w'
static const font_point_t pts_w[] = {
    PT(6, 38),  PT(18, 38), PT(28, 70), PT(44, 38),     PT(56, 38),
    PT(72, 70), PT(82, 38), PT(94, 38), PT(80, 80),     PT(65, 80),
    PT(50, 45), PT(35, 80), PT(20, 80), PT_CLOSE(6, 38)};
static const glyph_t glyph_w = {pts_w, 13, 110};

/* --- Digits --- */

// '0'
static const font_point_t pts_0[] = {
    PT(30, 20), PT(70, 20), PT(70, 80), PT(30, 80), PT_CLOSE(30, 20),
    PT(42, 30), PT(42, 70), PT(58, 70), PT(58, 30), PT_CLOSE(58, 30)};
static const glyph_t glyph_0 = {pts_0, 10, 90};

// '1'
static const font_point_t pts_1[] = {
    PT(44, 20), PT(56, 20), PT(56, 80), PT(44, 80),      PT_CLOSE(44, 20),
    PT(25, 35), PT(44, 20), PT(44, 30), PT_CLOSE(25, 35) // Serify bit
};
static const glyph_t glyph_1 = {pts_1, 9, 90};

// '2'
static const font_point_t pts_2[] = {
    PT(30, 20), PT(70, 20), PT(70, 45),      PT(42, 45), PT(42, 70),
    PT(70, 70), PT(70, 80), PT(30, 80),      PT(30, 35), PT(58, 35),
    PT(58, 30), PT(30, 30), PT_CLOSE(30, 20)};
static const glyph_t glyph_2 = {pts_2, 13, 90};

// '3'
static const font_point_t pts_3[] = {
    PT(30, 20), PT(70, 20), PT(70, 80),      PT(30, 80), PT(30, 70),
    PT(58, 70), PT(58, 55), PT(40, 55),      PT(40, 45), PT(58, 45),
    PT(58, 30), PT(30, 30), PT_CLOSE(30, 20)};
static const glyph_t glyph_3 = {pts_3, 13, 90};

// '4'
static const font_point_t pts_4[] = {
    PT(58, 20), PT(70, 20), PT(70, 80), PT(58, 80), PT_CLOSE(58, 20), // Stem
    PT(30, 20), PT(42, 20), PT(42, 50), PT(30, 50), PT_CLOSE(30, 20), // Left
    PT(30, 50), PT(58, 50), PT(58, 60), PT(30, 60), PT_CLOSE(30, 50)  // Bar
};
static const glyph_t glyph_4 = {pts_4, 15, 90};

// ':'
static const font_point_t pts_colon[] = {
    PT(42, 38), PT(58, 38), PT(58, 50), PT(42, 50), PT_CLOSE(42, 38),
    PT(42, 60), PT(58, 60), PT(58, 72), PT(42, 72), PT_CLOSE(42, 60)};
static const glyph_t glyph_colon = {pts_colon, 10, 80};

/* Glyph Lookup */
const glyph_t *font_get_glyph(char c) {
  if (c == 32) {
    static const glyph_t space = {NULL, 0, 50};
    return &space;
  }

  switch (c) {
  case 'G':
    return &glyph_G;
  case 'e':
    return &glyph_e;
  case 'm':
    return &glyph_m;
  case 'O':
    return &glyph_O;
  case 'S':
    return &glyph_S;
  case 'F':
    return &glyph_F;
  case 'i':
    return &glyph_i;
  case 'l':
    return &glyph_l;
  case 'E':
    return &glyph_E;
  case 'd':
    return &glyph_d;
  case 't':
    return &glyph_tee;
  case 'V':
    return &glyph_V;
  case 'w':
    return &glyph_w;
  case 'A':
    return &glyph_A;
  case 'p':
    return &glyph_p;
  case 'I':
    return &glyph_I;
  case 'L':
    return &glyph_L;

  case '0':
    return &glyph_0;
  case '1':
    return &glyph_1;
  case '2':
    return &glyph_2;
  case '3':
    return &glyph_3;
  case '4':
    return &glyph_4;

  case ':':
    return &glyph_colon;
  case 'a':
    return &glyph_a;
  case 's':
    return &glyph_s;

  default:
    return &glyph_box;
  }
}
