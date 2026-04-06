#include "scanline.h"
#include "../../include/string.h"
#include "../include/heap.h"

#define V_OVERSAMPLE 4
#define SUBPIXEL_SHIFT 0

void rasterizer_init(rasterizer_t *r, int width, int height, uint8_t *buffer) {
  if (!r)
    return;
  r->width = width;
  r->height = height;
  r->buffer = buffer;
  r->max_edges = MAX_EDGES;
  r->edges = (edge_t *)kalloc(r->max_edges * sizeof(edge_t));
  r->edge_count = 0;
}

void rasterizer_clear(rasterizer_t *r) {
  if (!r)
    return;
  r->edge_count = 0;
  if (r->buffer) {
    memset(r->buffer, 0, r->width * r->height);
  }
}

static void add_edge(rasterizer_t *r, float x0, float y0, float x1, float y1) {
  if (r->edge_count >= r->max_edges)
    return;

  /* Horizontal edges don't contribute to scanline intersections */
  if (y0 == y1)
    return;

  edge_t *e = &r->edges[r->edge_count++];
  if (y0 < y1) {
    e->x0 = x0;
    e->y0 = y0;
    e->x1 = x1;
    e->y1 = y1;
  } else {
    e->x0 = x1;
    e->y0 = y1;
    e->x1 = x0;
    e->y1 = y0;
  }
  e->min_y = e->y0;
}

static void decompose_quad(rasterizer_t *r, float x0, float y0, float cx,
                           float cy, float x1, float y1, int depth) {
  if (depth > 4) {
    add_edge(r, x0, y0, x1, y1);
    return;
  }

  // Midpoints
  float m01x = (x0 + cx) * 0.5f;
  float m01y = (y0 + cy) * 0.5f;
  float m12x = (cx + x1) * 0.5f;
  float m12y = (cy + y1) * 0.5f;
  float m012x = (m01x + m12x) * 0.5f;
  float m012y = (m01y + m12y) * 0.5f;

  decompose_quad(r, x0, y0, m01x, m01y, m012x, m012y, depth + 1);
  decompose_quad(r, m012x, m012y, m12x, m12y, x1, y1, depth + 1);
}

/*
 * Correct Decomposer logic
 */
static void add_contour_robust(rasterizer_t *r, tt_glyph_t *glyph, int start,
                               int end, float sx, float sy, float ox,
                               float oy) {
  int count = end - start + 1;

  /* Iterator */
  int i = 0;

  /* Current position (virtual on-curve) */
  float cx, cy;

  /* Determine initial P */
  if (glyph->points[start].on_curve) {
    cx = glyph->points[start].x * sx + ox;
    cy = glyph->points[start].y * sy + oy;
    i = 1;
  } else {
    if (glyph->points[end].on_curve) {
      cx = glyph->points[end].x * sx + ox;
      cy = glyph->points[end].y * sy + oy;
      i = 0; // We start processing from 0
    } else {
      // Both off, start at mid
      float x0 = glyph->points[start].x;
      float y0 = glyph->points[start].y;
      float xe = glyph->points[end].x;
      float ye = glyph->points[end].y;
      cx = ((x0 + xe) * 0.5f) * sx + ox;
      cy = ((y0 + ye) * 0.5f) * sy + oy;
      i = 0;
    }
  }

  float start_x = cx, start_y = cy;

  /* Process all points */
  int pts_processed = 0;
  while (pts_processed < count) {
    int idx = start + (i % count);
    int idx_next = start + ((i + 1) % count);

    bool on = glyph->points[idx].on_curve;
    float px = glyph->points[idx].x * sx + ox;
    float py = glyph->points[idx].y * sy + oy;

    /* Vertical Snapping (Pseudo-Hinting) */
    /* Snap to nearest pixel Y to sharpen horizontal lines */
    py = (float)((int)(py + 0.5f));

    if (on) {
      add_edge(r, cx, cy, px, py);
      cx = px;
      cy = py;
      i++;
      pts_processed++;
    } else {
      // Bezier Control Point
      float nx, ny;
      bool next_on = glyph->points[idx_next].on_curve;

      if (next_on) {
        nx = glyph->points[idx_next].x * sx + ox;
        ny = glyph->points[idx_next].y * sy + oy;
        decompose_quad(r, cx, cy, px, py, nx, ny, 0);
        cx = nx;
        cy = ny;
        i += 2; // Consumed control & next
        pts_processed += 2;
      } else {
        // Next is also Control -> Virtual On-Curve at midpoint
        float mx = glyph->points[idx_next].x * sx + ox;
        float my = glyph->points[idx_next].y * sy + oy;

        float midx = (px + mx) * 0.5f;
        float midy = (py + my) * 0.5f;

        decompose_quad(r, cx, cy, px, py, midx, midy, 0);
        cx = midx;
        cy = midy;
        i++; // Consumed only current control
        pts_processed++;
      }
    }
  }

  // Close loop
  add_edge(r, cx, cy, start_x, start_y);
}

/*
 * Rasterization Pass
 */

static void sort_floats(float *arr, int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        float t = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = t;
      }
    }
  }
}

void rasterizer_draw_glyph(rasterizer_t *r, tt_glyph_t *glyph, float scale_x,
                           float scale_y, float offset_x, float offset_y) {
  if (!r || !glyph)
    return;

  r->edge_count = 0;

  /* 1. Decompose to edges */
  for (int c = 0; c < glyph->contour_count; c++) {
    int start = (c == 0) ? 0 : (glyph->contours[c - 1] + 1);
    int end = glyph->contours[c];
    add_contour_robust(r, glyph, start, end, scale_x, scale_y, offset_x,
                       offset_y);
  }

  /* 2. Scanline Sweep */

  float *intersections = (float *)kalloc(sizeof(float) * MAX_EDGES);
  if (!intersections)
    return;

  int sub_height = r->height * V_OVERSAMPLE;

  for (int sy = 0; sy < sub_height; sy++) {
    float y_scan = (float)sy / V_OVERSAMPLE + (0.5f / V_OVERSAMPLE);
    int int_count = 0;

    for (int i = 0; i < r->edge_count; i++) {
      edge_t *e = &r->edges[i];

      // Check Y intersection
      if ((e->y0 <= y_scan && e->y1 > y_scan) ||
          (e->y1 <= y_scan && e->y0 > y_scan)) {
        // Calculate X
        float t = (y_scan - e->y0) / (e->y1 - e->y0);
        float x = e->x0 + t * (e->x1 - e->x0);

        if (int_count < MAX_EDGES) {
          intersections[int_count++] = x;
        }
      }
    }

    sort_floats(intersections, int_count);

    // Fill spans
    for (int i = 0; i < int_count; i += 2) {
      if (i + 1 >= int_count)
        break;

      float x_start = intersections[i];
      float x_end = intersections[i + 1];

      int p_start = (int)x_start;
      int p_end = (int)x_end;

      int y_pixel = sy / V_OVERSAMPLE;
      if (y_pixel >= r->height)
        continue;

      // Optimize: Clamp to screen
      if (p_start < 0)
        p_start = 0;
      if (p_start >= r->width)
        p_start = r->width - 1;
      if (p_end < 0)
        p_end = 0;
      if (p_end >= r->width)
        p_end = r->width - 1;

      int contribution = 255 / V_OVERSAMPLE;

      int ix_min = (int)x_start;
      int ix_max = (int)x_end;

      if (ix_min == ix_max) {
        float cov = x_end - x_start;
        uint32_t val = (uint32_t)(cov * contribution);
        int idx = y_pixel * r->width + ix_min;
        if (idx >= 0 && idx < r->width * r->height) {
          uint32_t old = r->buffer[idx];
          if (old + val > 255)
            r->buffer[idx] = 255;
          else
            r->buffer[idx] = old + val;
        }
      } else {
        // Start
        {
          float cov = (ix_min + 1) - x_start;
          uint32_t val = (uint32_t)(cov * contribution);
          int idx = y_pixel * r->width + ix_min;
          if (idx >= 0 && idx < r->width * r->height) {
            uint32_t old = r->buffer[idx];
            if (old + val > 255)
              r->buffer[idx] = 255;
            else
              r->buffer[idx] = old + val;
          }
        }

        // End
        {
          float cov = x_end - ix_max;
          uint32_t val = (uint32_t)(cov * contribution);
          int idx = y_pixel * r->width + ix_max;
          if (idx >= 0 && idx < r->width * r->height) {
            uint32_t old = r->buffer[idx];
            if (old + val > 255)
              r->buffer[idx] = 255;
            else
              r->buffer[idx] = old + val;
          }
        }

        // Middle
        for (int px = ix_min + 1; px < ix_max; px++) {
          int idx = y_pixel * r->width + px;
          if (idx >= 0 && idx < r->width * r->height) {
            uint32_t old = r->buffer[idx];
            if (old + contribution > 255)
              r->buffer[idx] = 255;
            else
              r->buffer[idx] = old + contribution;
          }
        }
      }
    }
  }

  kfree(intersections);
}
