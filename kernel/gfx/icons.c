/*
 * GemOS icons: 32x32 ARGB pixels (0xAARRGGBB), declared in icons.h.
 *
 * Each icon names its colours right above its data; one source line is
 * one row of pixels.
 */

#include "icons.h"

/* Terminal - green terminal with a >_ prompt */
#define _ 0x00000000 /* Transparent */
#define G 0xFF40C040 /* Green */
#define D 0xFF1A1A1A /* Dark */
static const uint32_t icon_terminal_data[32 * 32] = {
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,_,_,_,_,
    _,_,_,_,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,G,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,G,G,G,G,G,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,D,G,_,_,_,_,
    _,_,_,_,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,_,_,_,_,
    _,_,_,_,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,G,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
};
#undef _
#undef G
#undef D
const icon_t icon_terminal = {32, 32, icon_terminal_data};

/* About GemOS - teal diamond with a white G (GemOS logo) */
#define _ 0x00000000 /* Transparent */
#define T 0xFF00B5AD /* Teal - main */
#define W 0xFFFFFFFF /* White */
#define D 0xFF008A84 /* Teal dark */
static const uint32_t icon_about_data[32 * 32] = {
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,T,T,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,T,T,T,T,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,T,T,T,T,T,T,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,T,T,T,T,T,T,T,T,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,T,T,T,T,T,T,T,T,T,T,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,T,T,T,T,T,T,T,T,T,T,T,T,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,T,T,T,T,W,W,W,W,W,W,T,T,T,T,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,T,T,T,T,W,W,W,W,W,W,W,W,T,T,T,T,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,T,T,T,T,W,W,T,T,T,T,T,T,W,W,T,T,T,T,_,_,_,_,_,_,_,
    _,_,_,_,_,_,T,T,T,T,W,W,T,T,T,T,T,T,T,T,W,W,T,T,T,T,_,_,_,_,_,_,
    _,_,_,_,_,T,T,T,T,W,W,T,T,T,T,T,T,T,T,T,T,W,W,T,T,T,T,_,_,_,_,_,
    _,_,_,_,T,T,T,T,W,W,T,T,T,T,T,T,T,T,T,T,T,T,W,W,T,T,T,T,_,_,_,_,
    _,_,_,T,T,T,T,W,W,T,T,T,T,T,W,W,W,W,W,W,T,T,T,W,W,T,T,T,T,_,_,_,
    _,_,_,T,T,T,T,W,W,T,T,T,T,T,W,W,W,W,W,W,T,T,T,W,W,T,T,T,T,_,_,_,
    _,_,_,_,D,D,D,D,W,W,D,D,D,D,D,D,D,D,D,D,D,D,W,W,D,D,D,D,_,_,_,_,
    _,_,_,_,_,D,D,D,D,W,W,D,D,D,D,D,D,D,D,D,D,W,W,D,D,D,D,_,_,_,_,_,
    _,_,_,_,_,_,D,D,D,D,W,W,D,D,D,D,D,D,D,D,W,W,D,D,D,D,_,_,_,_,_,_,
    _,_,_,_,_,_,_,D,D,D,D,W,W,D,D,D,D,D,D,W,W,D,D,D,D,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,D,D,D,D,W,W,W,W,W,W,W,W,D,D,D,D,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,D,D,D,D,W,W,W,W,W,W,D,D,D,D,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,D,D,D,D,D,D,D,D,D,D,D,D,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,D,D,D,D,D,D,D,D,D,D,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,D,D,D,D,D,D,D,D,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,D,D,D,D,D,D,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,D,D,D,D,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,D,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
};
#undef _
#undef T
#undef W
#undef D
const icon_t icon_about = {32, 32, icon_about_data};

/* Text editor - white page with blue text lines */
#define _ 0x00000000 /* Transparent */
#define O 0xFF606060 /* Gray - outline */
#define W 0xFFFFFFFF /* White - page */
#define B 0xFF4080C0 /* Blue - text lines */
static const uint32_t icon_textedit_data[32 * 32] = {
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,B,B,B,B,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,B,B,B,B,B,B,B,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,B,B,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,B,B,B,B,B,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,B,B,B,B,B,B,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,B,B,B,B,B,B,B,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,O,_,_,_,_,_,_,
    _,_,_,_,_,_,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,O,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
};
#undef _
#undef O
#undef W
#undef B
const icon_t icon_textedit = {32, 32, icon_textedit_data};

/* Missing icon - red square (dock fallback) */
#define R 0xFFFF4040 /* Red */
static const uint32_t icon_missing_data[32 * 32] = {
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
    R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,R,
};
#undef R
const icon_t icon_missing = {32, 32, icon_missing_data};

/* Folder - gold folder with a shadow (file explorer) */
#define _T 0x00000000 /* Transparent */
#define F4 0xFFCC7700 /* Tab dark */
#define F3 0xFFF0C050 /* Gold highlight */
#define F1 0xFFE8A020 /* Gold main */
#define F2 0xFFD08810 /* Gold shadow */
static const uint32_t icon_folder_data[32 * 32] = {
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,F4,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F4,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F3,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F1,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F4,_T,_T,_T,_T,
    _T,_T,_T,F4,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F2,F4,_T,_T,_T,_T,
    _T,_T,_T,_T,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,F4,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
};
#undef _T
#undef F4
#undef F3
#undef F1
#undef F2
const icon_t icon_folder = {32, 32, icon_folder_data};

/* Text file - white page with blue lines and a folded corner (file explorer) */
#define _T 0x00000000 /* Transparent */
#define PB 0xFFDDDDDD /* Page border/shadow */
#define PG 0xFFF0F0F0 /* Page white */
#define FL 0xFFD0D0D0 /* Fold color */
#define LN 0xFF4488CC /* Line color (blue) */
static const uint32_t icon_textfile_data[32 * 32] = {
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,LN,LN,LN,LN,LN,LN,LN,LN,LN,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,LN,LN,LN,LN,LN,LN,LN,LN,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,LN,LN,LN,LN,LN,LN,LN,LN,LN,LN,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
};
#undef _T
#undef PB
#undef PG
#undef FL
#undef LN
const icon_t icon_textfile = {32, 32, icon_textfile_data};

/* Generic file - white page with a gray folded corner (file explorer) */
#define _T 0x00000000 /* Transparent */
#define PB 0xFFBBBBBB /* Page border */
#define PG 0xFFE8E8E8 /* Page gray-white */
#define FL 0xFFA0A0A0 /* Fold color */
static const uint32_t icon_generic_data[32 * 32] = {
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,FL,FL,FL,FL,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PG,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,PB,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
    _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
};
#undef _T
#undef PB
#undef PG
#undef FL
const icon_t icon_generic_file = {32, 32, icon_generic_data};
