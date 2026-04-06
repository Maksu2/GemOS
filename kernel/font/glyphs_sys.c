#include "font.h"
#include <stddef.h>

/*
   Solid Vector Font Data (Roboto Regular Style)
   Expanded for "The quick brown fox..." verification.
   Grid: 100x100
*/

#define PT(x, y) {x, y, false}
#define PT_CLOSE(x, y) {x, y, true}

static const font_point_t pts_box[] = {PT(10, 10), PT(90, 10), PT(90, 90),
                                       PT_CLOSE(10, 90)};
static const glyph_t glyph_box = {pts_box, 4, 90};

/* --- Uppercase --- */
// T
static const font_point_t pts_T[] = {PT(10, 20), PT(90, 20), PT(90, 32),
                                     PT(56, 32), PT(56, 80), PT(44, 80),
                                     PT(44, 32), PT(10, 32), PT_CLOSE(10, 20)};
static const glyph_t glyph_T = {pts_T, 9, 95};

/* --- Lowercase --- */
// h (stem + arch)
static const font_point_t pts_h[] = {
    PT(20, 10), PT(32, 10), PT(32, 45), PT(45, 38), PT(65, 38),
    PT(80, 50), PT(80, 80), PT(68, 80), PT(68, 52), PT(58, 48),
    PT(40, 48), PT(32, 60), PT(32, 80), PT(20, 80), PT_CLOSE(20, 10)};
static const glyph_t glyph_h = {pts_h, 15, 95};

// e (existing)
static const font_point_t pts_e[] = {
    PT(20, 56),       PT(82, 56), PT(82, 48), PT(60, 38), PT(30, 38),
    PT(16, 48),       PT(16, 70), PT(30, 80), PT(65, 80), PT(78, 72),
    PT(78, 62),       PT(66, 62), PT(66, 68), PT(30, 68), PT(30, 56),
    PT_CLOSE(20, 56), PT(30, 48), PT(68, 48), PT(68, 50), PT(30, 50),
    PT_CLOSE(30, 48)};
static const glyph_t glyph_e = {pts_e, 17, 90};

// q (b mirrored + tail)
static const font_point_t pts_q[] = {
    PT(68, 38), PT(80, 38), PT(80, 100), PT(68, 100), PT_CLOSE(68, 38),
    PT(20, 38), PT(68, 38), PT(68, 80),  PT(20, 80),  PT_CLOSE(20, 38),
    PT(32, 48), PT(56, 48), PT(56, 70),  PT(32, 70),  PT_CLOSE(32, 48)};
static const glyph_t glyph_q = {pts_q, 15, 95};

// u (cup)
static const font_point_t pts_u[] = {
    PT(20, 38), PT(32, 38), PT(32, 70), PT(40, 80), PT(60, 80),
    PT(68, 70), PT(68, 38), PT(80, 38), PT(80, 80), PT(68, 80),
    PT(68, 75), PT(60, 82), PT(40, 82), PT(20, 70), PT_CLOSE(20, 38)};
static const glyph_t glyph_u = {pts_u, 15, 95};

// i (existing)
static const font_point_t pts_i[] = {
    PT(44, 20), PT(56, 20), PT(56, 30), PT(44, 30), PT_CLOSE(44, 20),
    PT(44, 38), PT(56, 38), PT(56, 80), PT(44, 80), PT_CLOSE(44, 38)};
static const glyph_t glyph_i = {pts_i, 10, 50};

// c (C)
static const font_point_t pts_c[] = {
    PT(75, 45), PT(65, 45), PT(60, 42), PT(40, 42),      PT(32, 50),
    PT(32, 70), PT(40, 78), PT(60, 78), PT(65, 75),      PT(75, 75),
    PT(70, 82), PT(60, 90), PT(30, 90), PT(18, 75),      PT(18, 45),
    PT(30, 35), PT(60, 35), PT(70, 40), PT_CLOSE(75, 45)};
static const glyph_t glyph_c = {pts_c, 19, 85};

// k (stem + legs)
static const font_point_t pts_k[] = {
    PT(20, 10), PT(32, 10), PT(32, 80), PT(20, 80),      PT_CLOSE(20, 10),
    PT(70, 38), PT(50, 60), PT(32, 50), PT(32, 65),      PT(55, 80),
    PT(72, 80), PT(52, 65), PT(70, 45), PT_CLOSE(70, 38)};
static const glyph_t glyph_k = {pts_k, 14, 85};

// b (d mirrored)
static const font_point_t pts_b[] = {
    PT(20, 10), PT(32, 10), PT(32, 80), PT(20, 80), PT_CLOSE(20, 10),
    PT(32, 38), PT(80, 38), PT(80, 80), PT(32, 80), PT_CLOSE(32, 38),
    PT(44, 48), PT(68, 48), PT(68, 70), PT(44, 70), PT_CLOSE(44, 48)};
static const glyph_t glyph_b = {pts_b, 15, 95};

// r (stem + ear)
static const font_point_t pts_r[] = {
    PT(20, 38),       PT(32, 38), PT(32, 80), PT(20, 80),
    PT_CLOSE(20, 38), PT(32, 45), PT(50, 38), PT(65, 38),
    PT(65, 48),       PT(50, 48), PT(32, 60), PT_CLOSE(32, 45)};
static const glyph_t glyph_r = {pts_r, 12, 70};

// o (donut)
static const font_point_t pts_o[] = {
    PT(30, 38), PT(70, 38), PT(82, 50), PT(82, 70),       PT(70, 82),
    PT(30, 82), PT(18, 70), PT(18, 50), PT_CLOSE(30, 38), PT(30, 48),
    PT(30, 72), PT(70, 72), PT(70, 48), PT_CLOSE(30, 48)};
static const glyph_t glyph_o = {pts_o, 14, 95};

// w (from before)
static const font_point_t pts_w[] = {
    PT(6, 38),  PT(18, 38), PT(28, 70), PT(44, 38),     PT(56, 38),
    PT(72, 70), PT(82, 38), PT(94, 38), PT(80, 80),     PT(65, 80),
    PT(50, 45), PT(35, 80), PT(20, 80), PT_CLOSE(6, 38)};
static const glyph_t glyph_w = {pts_w, 14, 110};

// n (stem + arch)
static const font_point_t pts_n[] = {
    PT(20, 38),       PT(32, 38), PT(32, 80), PT(20, 80),
    PT_CLOSE(20, 38), PT(32, 45), PT(50, 38), PT(70, 38),
    PT(80, 50),       PT(80, 80), PT(68, 80), PT(68, 55),
    PT(60, 48),       PT(45, 48), PT(32, 60), PT_CLOSE(32, 45)};
static const glyph_t glyph_n = {pts_n, 16, 95};

// f (hook)
static const font_point_t pts_f[] = {
    PT(40, 10), PT(60, 10), PT(65, 20), PT(50, 20), PT(45, 30),      PT(45, 38),
    PT(65, 38), PT(65, 48), PT(45, 48), PT(45, 80), PT(33, 80),      PT(33, 48),
    PT(20, 48), PT(20, 38), PT(33, 38), PT(33, 20), PT_CLOSE(40, 10)};
static const glyph_t glyph_f = {pts_f, 17, 70};

// x (cross)
static const font_point_t pts_x[] = {
    PT(20, 38), PT(35, 38), PT(50, 55),      PT(65, 38), PT(80, 38),
    PT(60, 60), PT(80, 80), PT(65, 80),      PT(50, 65), PT(35, 80),
    PT(20, 80), PT(40, 60), PT_CLOSE(20, 38)};
static const glyph_t glyph_x = {pts_x, 13, 90};

// j (hook down)
static const font_point_t pts_j[] = {
    PT(44, 20), PT(56, 20), PT(56, 30),      PT(44, 30),  PT_CLOSE(44, 20),
    PT(44, 38), PT(56, 38), PT(56, 90),      PT(40, 100), PT(20, 100),
    PT(20, 90), PT(44, 85), PT_CLOSE(44, 38)};
static const glyph_t glyph_j = {pts_j, 13, 60};

// p (existing)
static const font_point_t pts_p[] = {
    PT(20, 38), PT(32, 38), PT(32, 100), PT(20, 100), PT_CLOSE(20, 38),
    PT(32, 38), PT(80, 38), PT(80, 80),  PT(32, 80),  PT_CLOSE(32, 38),
    PT(44, 48), PT(68, 48), PT(68, 70),  PT(44, 70),  PT_CLOSE(44, 48)};
static const glyph_t glyph_p = {pts_p, 15, 95};

// s (existing)
static const font_point_t pts_s[] = {
    PT(65, 38), PT(35, 38), PT(25, 42), PT(25, 52), PT(65, 55),
    PT(70, 60), PT(70, 75), PT(65, 80), PT(25, 80), PT(25, 72),
    PT(37, 72), PT(37, 70), PT(60, 70), PT(58, 62), PT(30, 60),
    PT(25, 55), PT(35, 46), PT(35, 46), PT(65, 46), PT_CLOSE(65, 38)};
static const glyph_t glyph_s = {pts_s, 20, 85};

// m (existing)
static const font_point_t pts_m[] = {
    PT(10, 38), PT(22, 38),       PT(22, 80), PT(10, 80),      PT_CLOSE(10, 38),
    PT(22, 38), PT(48, 38),       PT(48, 80), PT(36, 80),      PT(36, 50),
    PT(22, 50), PT_CLOSE(22, 38), PT(48, 38), PT(74, 38),      PT(74, 80),
    PT(62, 80), PT(62, 50),       PT(48, 50), PT_CLOSE(48, 38)};
static const glyph_t glyph_m = {pts_m, 19, 105};

// v (wedge)
static const font_point_t pts_v[] = {PT(20, 38), PT(32, 38),      PT(50, 70),
                                     PT(68, 38), PT(80, 38),      PT(56, 80),
                                     PT(44, 80), PT_CLOSE(20, 38)};
static const glyph_t glyph_v = {pts_v, 8, 90};

// l (existing)
static const font_point_t pts_l[] = {PT(44, 20), PT(56, 20), PT(56, 80),
                                     PT(44, 80), PT_CLOSE(44, 20)};
static const glyph_t glyph_l = {pts_l, 5, 50};

// z (zig zag)
static const font_point_t pts_z[] = {
    PT(20, 38), PT(80, 38), PT(80, 48), PT(40, 70), PT(80, 70),      PT(80, 80),
    PT(20, 80), PT(20, 70), PT(60, 48), PT(20, 48), PT_CLOSE(20, 38)};
static const glyph_t glyph_z = {pts_z, 12, 90};

// y (v tail)
static const font_point_t pts_y[] = {PT(20, 38),  PT(32, 38), PT(50, 70),
                                     PT(68, 38),  PT(80, 38), PT(50, 100),
                                     PT(35, 100), PT(42, 80), PT_CLOSE(20, 38)};
static const glyph_t glyph_y = {pts_y, 11, 95};

// d (existing)
static const font_point_t pts_d[] = {
    PT(68, 20), PT(80, 20), PT(80, 80), PT(68, 80), PT_CLOSE(68, 20),
    PT(20, 38), PT(68, 38), PT(68, 80), PT(20, 80), PT_CLOSE(20, 38),
    PT(32, 48), PT(32, 70), PT(56, 70), PT(56, 48), PT_CLOSE(56, 48)};
static const glyph_t glyph_d = {pts_d, 15, 95};

// g (simple single story)
static const font_point_t pts_g[] = {
    PT(68, 38),  PT(80, 38), PT(80, 85),       PT(60, 100),
    PT(20, 100), PT(20, 88), PT(68, 88),       PT(68, 80),
    PT(20, 80),  PT(20, 38), PT_CLOSE(68, 38), PT(32, 48),
    PT(68, 48),  PT(68, 70), PT(32, 70),       PT_CLOSE(32, 48)};
static const glyph_t glyph_g = {pts_g, 16, 95};

/* Lookup */
const glyph_t *font_get_glyph_sys(char c) {
  if (c == 32) {
    static const glyph_t space = {NULL, 0, 40};
    return &space;
  }
  switch (c) {
  case 'T':
    return &glyph_T;
  case 'h':
    return &glyph_h;
  case 'e':
    return &glyph_e;
  case 'q':
    return &glyph_q;
  case 'u':
    return &glyph_u;
  case 'i':
    return &glyph_i;
  case 'c':
    return &glyph_c;
  case 'k':
    return &glyph_k;
  case 'b':
    return &glyph_b;
  case 'r':
    return &glyph_r;
  case 'o':
    return &glyph_o;
  case 'w':
    return &glyph_w;
  case 'n':
    return &glyph_n;
  case 'f':
    return &glyph_f;
  case 'x':
    return &glyph_x;
  case 'j':
    return &glyph_j;
  case 'm':
    return &glyph_m;
  case 'p':
    return &glyph_p;
  case 's':
    return &glyph_s;
  case 'v':
    return &glyph_v;
  case 'l':
    return &glyph_l;
  case 'z':
    return &glyph_z;
  case 'y':
    return &glyph_y;
  case 'd':
    return &glyph_d;
  case 'g':
    return &glyph_g;
  default:
    return &glyph_box;
  }
}
