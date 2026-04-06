#include "truetype.h"
#include "../../include/string.h"
#include "../gfx/primitives.h" // For kprintf/serial debugging if needed
#include "../include/heap.h"

/*
 * Helper Functions
 */

static uint8_t read_u8(tt_font_t *font, uint32_t offset) {
  if (offset >= font->size)
    return 0;
  return font->data[offset];
}

static uint16_t read_u16(tt_font_t *font, uint32_t offset) {
  if (offset + 1 >= font->size)
    return 0;
  uint16_t val = *(uint16_t *)(font->data + offset);
  return TT_SWAP16(val);
}

static int16_t read_i16(tt_font_t *font, uint32_t offset) {
  return (int16_t)read_u16(font, offset);
}

static uint32_t read_u32(tt_font_t *font, uint32_t offset) {
  if (offset + 3 >= font->size)
    return 0;
  uint32_t val = *(uint32_t *)(font->data + offset);
  return TT_SWAP32(val);
}

/*
 * Table Parsing
 */

static uint32_t find_table(tt_font_t *font, const char *tag) {
  uint32_t num_tables = read_u16(font, 4);
  uint32_t p = 12; // Start of Table Directory

  for (uint32_t i = 0; i < num_tables; i++) {
    // Tag is at p
    if (font->data[p] == tag[0] && font->data[p + 1] == tag[1] &&
        font->data[p + 2] == tag[2] && font->data[p + 3] == tag[3]) {
      return read_u32(font, p + 8); // Offset
    }
    p += 16;
  }
  return 0;
}

bool tt_load(tt_font_t *font, uint8_t *data, size_t size) {
  if (!font || !data)
    return false;

  memset(font, 0, sizeof(tt_font_t));
  font->data = data;
  font->size = size;

  /* Verify Scaler Type (0x00010000 or 'true' or 'typ1') */
  uint32_t scaler = read_u32(font, 0);
  if (scaler != 0x00010000 && scaler != 0x74727565) {
    return false;
  }

  /* Find Required Tables */
  font->head_offset = find_table(font, "head");
  font->maxp_offset = find_table(font, "maxp");
  font->hhea_offset = find_table(font, "hhea");
  font->hmtx_offset = find_table(font, "hmtx");
  font->cmap_offset = find_table(font, "cmap");
  font->glyf_offset = find_table(font, "glyf");
  font->loca_offset = find_table(font, "loca");

  if (!font->head_offset || !font->maxp_offset || !font->hhea_offset ||
      !font->hmtx_offset || !font->cmap_offset || !font->glyf_offset ||
      !font->loca_offset) {
    return false;
  }

  /* Parse HEAD */
  font->units_per_em = read_u16(font, font->head_offset + 18);
  font->index_to_loc_format = read_i16(font, font->head_offset + 50);

  /* Parse MAXP */
  font->num_glyphs = read_u16(font, font->maxp_offset + 4);

  /* Parse HHEA */
  font->ascender = read_i16(font, font->hhea_offset + 4);
  font->descender = read_i16(font, font->hhea_offset + 6);
  font->line_gap = read_i16(font, font->hhea_offset + 8);
  font->num_of_long_hor_metrics = read_u16(font, font->hhea_offset + 34);

  return true;
}

uint16_t tt_get_glyph_index(tt_font_t *font, uint32_t codepoint) {
  if (!font->cmap_offset)
    return 0;

  uint32_t cmap = font->cmap_offset;
  uint16_t num_tables = read_u16(font, cmap + 2);

  uint32_t encoding_record_offset = cmap + 4;
  uint32_t selected_subtable_offset = 0;

  /* Look for Platform 3 (Windows), Encoding 1 (Unicode BMP) */
  for (int i = 0; i < num_tables; i++) {
    uint16_t platform = read_u16(font, encoding_record_offset);
    uint16_t encoding = read_u16(font, encoding_record_offset + 2);
    uint32_t offset = read_u32(font, encoding_record_offset + 4);

    if (platform == 3 && encoding == 1) {
      selected_subtable_offset = cmap + offset;
      break;
    }
    encoding_record_offset += 8;
  }

  /* Fallback: Platform 0 (Unicode) */
  if (!selected_subtable_offset) {
    encoding_record_offset = cmap + 4;
    for (int i = 0; i < num_tables; i++) {
      uint16_t platform = read_u16(font, encoding_record_offset);
      uint32_t offset = read_u32(font, encoding_record_offset + 4);

      if (platform == 0) {
        selected_subtable_offset = cmap + offset;
        break;
      }
      encoding_record_offset += 8;
    }
  }

  if (!selected_subtable_offset)
    return 0;

  uint16_t format = read_u16(font, selected_subtable_offset);

  if (format == 4) {
    uint32_t p = selected_subtable_offset;
    uint16_t seg_count_x2 = read_u16(font, p + 6);
    uint16_t seg_count = seg_count_x2 / 2;

    uint32_t end_code_offset = p + 14;
    uint32_t start_code_offset =
        end_code_offset + seg_count_x2 + 2; // +2 for reservedPad
    uint32_t id_delta_offset = start_code_offset + seg_count_x2;
    uint32_t id_range_offset_offset = id_delta_offset + seg_count_x2;

    /* Find Segment */
    uint16_t segment = 0xFFFF;
    for (int i = 0; i < seg_count; i++) {
      uint16_t end_code = read_u16(font, end_code_offset + i * 2);
      if (codepoint <= end_code) {
        segment = i;
        break;
      }
    }

    if (segment == 0xFFFF)
      return 0;

    uint16_t start_code = read_u16(font, start_code_offset + segment * 2);
    if (codepoint < start_code)
      return 0;

    int16_t id_delta = read_i16(font, id_delta_offset + segment * 2);
    uint16_t id_range_offset =
        read_u16(font, id_range_offset_offset + segment * 2);

    if (id_range_offset == 0) {
      return (codepoint + id_delta) & 0xFFFF;
    } else {
      uint32_t glyph_index_offset = (id_range_offset_offset + segment * 2) +
                                    id_range_offset +
                                    (codepoint - start_code) * 2;
      uint16_t glyph_index = read_u16(font, glyph_index_offset);
      if (glyph_index == 0)
        return 0;
      return (glyph_index + id_delta) & 0xFFFF;
    }
  }

  return 0;
}

/*
 * Glyph Loading
 */

static uint32_t get_glyph_offset(tt_font_t *font, uint16_t index) {
  if (index >= font->num_glyphs)
    return 0;

  uint32_t offset = font->loca_offset;
  uint32_t off1, off2;

  if (font->index_to_loc_format == 0) {
    // Short: offset / 2
    off1 = read_u16(font, offset + index * 2) * 2;
    off2 = read_u16(font, offset + (index + 1) * 2) * 2;
  } else {
    // Long: offset
    off1 = read_u32(font, offset + index * 4);
    off2 = read_u32(font, offset + (index + 1) * 4);
  }

  if (off1 == off2)
    return 0; // Empty glyph
  return font->glyf_offset + off1;
}

void tt_free_glyph(tt_glyph_t *glyph) {
  if (!glyph)
    return;
  if (glyph->points)
    kfree(glyph->points);
  if (glyph->contours)
    kfree(glyph->contours);
  glyph->points = NULL;
  glyph->contours = NULL;
  glyph->point_count = 0;
  glyph->contour_count = 0;
}

/*
 * Composite Glyph Flags
 */
#define ARG_1_AND_2_ARE_WORDS (1 << 0)
#define ARGS_ARE_XY_VALUES (1 << 1)
#define ROUND_XY_TO_GRID (1 << 2)
#define WE_HAVE_A_SCALE (1 << 3)
#define MORE_COMPONENTS (1 << 5)
#define WE_HAVE_AN_X_AND_Y_SCALE (1 << 6)
#define WE_HAVE_A_TWO_BY_TWO (1 << 7)
#define WE_HAVE_INSTRUCTIONS (1 << 8)
#define USE_MY_METRICS (1 << 9)
#define OVERLAP_COMPOUND (1 << 10)

/* Forward declaration for recursion */
static bool internal_load_glyph(tt_font_t *font, uint16_t glyph_index,
                                tt_glyph_t *out_glyph, int depth);

/* Merge a subglyph into the main glyph with transformation */
static bool merge_glyph(tt_glyph_t *base, tt_glyph_t *sub, float a, float b,
                        float c, float d, float e, float f) {
  /* Reallocate points */
  int new_point_count = base->point_count + sub->point_count;
  tt_point_t *new_points =
      (tt_point_t *)kalloc(new_point_count * sizeof(tt_point_t));
  if (!new_points)
    return false;

  /* Reallocate contours */
  int new_contour_count = base->contour_count + sub->contour_count;
  int *new_contours = (int *)kalloc(new_contour_count * sizeof(int));
  if (!new_contours) {
    kfree(new_points);
    return false;
  }

  /* Copy existing data */
  if (base->point_count > 0) {
    memcpy(new_points, base->points, base->point_count * sizeof(tt_point_t));
  }
  if (base->contour_count > 0) {
    memcpy(new_contours, base->contours, base->contour_count * sizeof(int));
  }

  /* Transform and append new points */
  for (int i = 0; i < sub->point_count; i++) {
    float x = sub->points[i].x;
    float y = sub->points[i].y;

    /* Affine Transform:
       x' = a*x + c*y + e
       y' = b*x + d*y + f
    */
    new_points[base->point_count + i].x = a * x + c * y + e;
    new_points[base->point_count + i].y = b * x + d * y + f;
    new_points[base->point_count + i].on_curve = sub->points[i].on_curve;
  }

  /* Append Contours (shifted by previous point count) */
  for (int i = 0; i < sub->contour_count; i++) {
    new_contours[base->contour_count + i] =
        sub->contours[i] + base->point_count;
  }

  /* Update Base */
  if (base->points)
    kfree(base->points);
  if (base->contours)
    kfree(base->contours);

  base->points = new_points;
  base->contours = new_contours;
  base->point_count = new_point_count;
  base->contour_count = new_contour_count;

  return true;
}

static bool load_composite_glyph(tt_font_t *font, tt_glyph_t *out_glyph,
                                 uint32_t offset, int depth) {
  /* Pointer is already past metrics (10 bytes) */
  uint32_t p = offset + 10;
  uint16_t flags = MORE_COMPONENTS;

  while (flags & MORE_COMPONENTS) {
    flags = read_u16(font, p);
    p += 2;
    uint16_t sub_index = read_u16(font, p);
    p += 2;

    /* Arguments */
    int32_t arg1, arg2;
    if (flags & ARG_1_AND_2_ARE_WORDS) {
      arg1 = read_i16(font, p);
      p += 2;
      arg2 = read_i16(font, p);
      p += 2;
    } else {
      arg1 = (int8_t)read_u8(font, p++);
      arg2 = (int8_t)read_u8(font, p++);
    }

    /* Transform */
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
    float e = 0.0f, f = 0.0f; /* Translation */

    if (flags & WE_HAVE_A_SCALE) {
      float s = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      a = s;
      d = s;
    } else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) {
      float sx = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      float sy = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      a = sx;
      d = sy;
    } else if (flags & WE_HAVE_A_TWO_BY_TWO) {
      a = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      b = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      c = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
      d = (int16_t)read_u16(font, p) / 16384.0f;
      p += 2;
    }

    /* Calculate Translation */
    if (flags & ARGS_ARE_XY_VALUES) {
      e = (float)arg1;
      f = (float)arg2;
    } else {
      /* Matching points not supported properly yet, assumed 0 */
      e = 0;
      f = 0;
    }

    /* Load Subglyph recursively */
    tt_glyph_t sub;
    memset(&sub, 0, sizeof(tt_glyph_t));
    if (internal_load_glyph(font, sub_index, &sub, depth + 1)) {
      if (!merge_glyph(out_glyph, &sub, a, b, c, d, e, f)) {
        tt_free_glyph(&sub);
        return false;
      }
      tt_free_glyph(&sub);
    }
  }

  return true;
}

static bool internal_load_simple_glyph(tt_font_t *font, tt_glyph_t *out_glyph,
                                       uint32_t offset, int16_t num_contours) {
  out_glyph->contour_count = num_contours;
  out_glyph->contours = (int *)kalloc(num_contours * sizeof(int));
  if (!out_glyph->contours)
    return false;

  uint32_t p = offset + 10;
  int point_count = 0;

  /* Read End Points of Contours */
  for (int i = 0; i < num_contours; i++) {
    out_glyph->contours[i] = read_u16(font, p);
    p += 2;
  }
  point_count = out_glyph->contours[num_contours - 1] + 1;
  out_glyph->point_count = point_count;

  /* Instructions */
  uint16_t ins_len = read_u16(font, p);
  p += 2 + ins_len;

  /* Flags */
  uint8_t *flags = (uint8_t *)kalloc(point_count); // Temp alloc
  if (!flags) {
    tt_free_glyph(out_glyph);
    return false;
  }

  for (int i = 0; i < point_count; i++) {
    uint8_t flag = read_u8(font, p++);
    flags[i] = flag;
    if (flag & 8) { // REPEAT
      uint8_t repeat_count = read_u8(font, p++);
      for (int k = 0; k < repeat_count; k++) {
        if (i + 1 < point_count) {
          flags[++i] = flag;
        }
      }
    }
  }

  /* Allocate Points */
  out_glyph->points = (tt_point_t *)kalloc(point_count * sizeof(tt_point_t));
  if (!out_glyph->points) {
    kfree(flags);
    tt_free_glyph(out_glyph);
    return false;
  }

  /* Read Coordinates */
  // X Coordinates
  int32_t current_x = 0;
  for (int i = 0; i < point_count; i++) {
    uint8_t flag = flags[i];
    int16_t dx = 0;

    if (flag & 2) { // X_SHORT
      uint8_t val = read_u8(font, p++);
      dx = (flag & 16) ? val : -val; // X_POSITIVE_OR_SAME
    } else {
      if (flag & 16) { // X_SAME
        dx = 0;
      } else {
        dx = read_i16(font, p);
        p += 2;
      }
    }
    current_x += dx;
    out_glyph->points[i].x = (float)current_x;
    out_glyph->points[i].on_curve = (flag & 1);
  }

  // Y Coordinates
  int32_t current_y = 0;
  for (int i = 0; i < point_count; i++) {
    uint8_t flag = flags[i];
    int16_t dy = 0;

    if (flag & 4) { // Y_SHORT
      uint8_t val = read_u8(font, p++);
      dy = (flag & 32) ? val : -val; // Y_POSITIVE_OR_SAME
    } else {
      if (flag & 32) { // Y_SAME
        dy = 0;
      } else {
        dy = read_i16(font, p);
        p += 2;
      }
    }
    current_y += dy;
    out_glyph->points[i].y = (float)current_y;
  }

  kfree(flags);
  return true;
}

static bool internal_load_glyph(tt_font_t *font, uint16_t glyph_index,
                                tt_glyph_t *out_glyph, int depth) {
  if (depth > 8)
    return false; // Loop protection

  uint32_t offset = get_glyph_offset(font, glyph_index);
  if (offset == 0) {
    if (depth == 0) {
      // Allow empty root glyphs (space)
      return true;
    }
    return false; // Components shouldn't be empty?
  }

  int16_t num_contours = read_i16(font, offset);

  /* Only root reads metrics separately or if USE_MY_METRICS (TODO) */
  if (depth == 0) {
    out_glyph->x_min = read_i16(font, offset + 2);
    out_glyph->y_min = read_i16(font, offset + 4);
    out_glyph->x_max = read_i16(font, offset + 6);
    out_glyph->y_max = read_i16(font, offset + 8);
  }

  if (num_contours >= 0) {
    return internal_load_simple_glyph(font, out_glyph, offset, num_contours);
  } else {
    return load_composite_glyph(font, out_glyph, offset, depth);
  }
}

bool tt_load_glyph(tt_font_t *font, uint16_t glyph_index,
                   tt_glyph_t *out_glyph) {
  if (!font || !out_glyph)
    return false;
  memset(out_glyph, 0, sizeof(tt_glyph_t));

  /* Metrics from HMTX (Always needed) */
  if (font->hmtx_offset) {
    uint32_t offset = font->hmtx_offset;
    if (glyph_index < font->num_of_long_hor_metrics) {
      out_glyph->advance_width = read_u16(font, offset + glyph_index * 4);
      out_glyph->lsb = read_i16(font, offset + glyph_index * 4 + 2);
    } else {
      // Last valid advance width
      out_glyph->advance_width =
          read_u16(font, offset + (font->num_of_long_hor_metrics - 1) * 4);
      out_glyph->lsb =
          read_i16(font, offset + font->num_of_long_hor_metrics * 4 +
                             (glyph_index - font->num_of_long_hor_metrics) * 2);
    }
  }

  return internal_load_glyph(font, glyph_index, out_glyph, 0);
}
