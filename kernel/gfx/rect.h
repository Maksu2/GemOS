#ifndef GFX_RECT_H
#define GFX_RECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int x;
  int y;
  int w;
  int h;
} rect_t;

/* Check if point is inside rect */
bool rect_contains(rect_t *r, int x, int y);

/* Intersect two rects, result stored in dest. Returns true if intersection is
 * not empty */
bool rect_intersect(rect_t *a, rect_t *b, rect_t *dest);

#endif /* GFX_RECT_H */
