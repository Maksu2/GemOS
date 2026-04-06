#ifndef TRUETYPE_H
#define TRUETYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * TrueType Data Types (Big Endian in file)
 */

typedef int16_t TT_FWord;
typedef uint16_t TT_UFWord;
typedef int16_t TT_Short;
typedef uint16_t TT_UShort;
typedef int32_t TT_Long;
typedef uint32_t TT_ULong;
typedef int32_t TT_Fixed;

/*
 * Macros for Endian Swapping
 * TTF files are Big Endian. x86 is Little Endian.
 */
#define TT_SWAP16(x) ((uint16_t)((((x) & 0xFF) << 8) | (((x) & 0xFF00) >> 8)))
#define TT_SWAP32(x)                                                           \
  ((uint32_t)((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) |                   \
              (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24)))

/*
 * Data Structures
 */

typedef struct {
  float x;
  float y;
  bool on_curve;
} tt_point_t;

/* contour_t is defined dynamically during parsing */

/*
 * Table Headers
 */

/* Offset Table */
typedef struct {
  TT_ULong scaler_type;
  TT_UShort num_tables;
  TT_UShort search_range;
  TT_UShort entry_selector;
  TT_UShort range_shift;
} __attribute__((packed)) tt_offset_table_t;

/* Table Directory Entry */
typedef struct {
  char tag[4];
  TT_ULong check_sum;
  TT_ULong offset;
  TT_ULong length;
} __attribute__((packed)) tt_table_dir_entry_t;

/* Head Table */
typedef struct {
  TT_Fixed version;
  TT_Fixed font_revision;
  TT_ULong check_sum_adjustment;
  TT_ULong magic_number;
  TT_UShort flags;
  TT_UShort units_per_em;
  long long created;  // 64-bit
  long long modified; // 64-bit
  TT_Short x_min;
  TT_Short y_min;
  TT_Short x_max;
  TT_Short y_max;
  TT_UShort mac_style;
  TT_UShort lowest_rec_ppem;
  TT_Short font_direction_hint;
  TT_Short index_to_loc_format; // 0 for Short, 1 for Long
  TT_Short glyph_data_format;
} __attribute__((packed)) tt_head_table_t;

/* Maxp Table */
typedef struct {
  TT_Fixed version;
  TT_UShort num_glyphs;
  /* ... incomplete, we only typically need num_glyphs */
} __attribute__((packed)) tt_maxp_table_t;

/* Hhea Table */
typedef struct {
  TT_Fixed version;
  TT_Short ascender;
  TT_Short descender;
  TT_Short line_gap;
  TT_UFWord advance_width_max;
  TT_FWord min_left_side_bearing;
  TT_FWord min_right_side_bearing;
  TT_FWord x_max_extent;
  TT_Short caret_slope_rise;
  TT_Short caret_slope_run;
  TT_Short caret_offset;
  TT_Short reserved[4];
  TT_Short metric_data_format;
  TT_UShort num_of_long_hor_metrics;
} __attribute__((packed)) tt_hhea_table_t;

/* Hmtx Table Entry (repeated) */
typedef struct {
  TT_UFWord advance_width;
  TT_FWord lsb;
} __attribute__((packed)) tt_hmtx_entry_t;

/*
 * Font Context
 */
typedef struct {
  uint8_t *data;
  size_t size;

  /* Table Offsets (already parsed from directory) */
  uint32_t head_offset;
  uint32_t maxp_offset;
  uint32_t hhea_offset;
  uint32_t hmtx_offset;
  uint32_t cmap_offset;
  uint32_t glyf_offset;
  uint32_t loca_offset;

  /* Cached Head values */
  uint16_t units_per_em;
  int16_t index_to_loc_format;

  /* Cached Hhea values */
  int16_t ascender;
  int16_t descender;
  int16_t line_gap;
  uint16_t num_of_long_hor_metrics;

  /* Cached Maxp values */
  uint16_t num_glyphs;

} tt_font_t;

/*
 * Glyph Data
 */
typedef struct {
  tt_point_t *points;
  int point_count;
  int *contours; // Array of end indices for contours
  int contour_count;

  /* Metrics */
  int advance_width;
  int lsb;
  int x_min;
  int y_min;
  int x_max;
  int y_max;
} tt_glyph_t;

/*
 * API
 */

/* Init font context from raw memory */
bool tt_load(tt_font_t *font, uint8_t *data, size_t size);

/* Map UTF-8/ASCII character to Glyph Index */
uint16_t tt_get_glyph_index(tt_font_t *font, uint32_t codepoint);

/* Load Glyph Data (Allocates memory, must be freed) */
bool tt_load_glyph(tt_font_t *font, uint16_t glyph_index,
                   tt_glyph_t *out_glyph);

/* Free Glyph Data */
void tt_free_glyph(tt_glyph_t *glyph);

#endif
