#include "rect.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

bool rect_is_empty(rect_t *r) { return (r->w <= 0 || r->h <= 0); }

bool rect_contains(rect_t *r, int x, int y) {
  return (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h);
}

bool rect_intersect(rect_t *a, rect_t *b, rect_t *dest) {
  int x1 = MAX(a->x, b->x);
  int y1 = MAX(a->y, b->y);
  int x2 = MIN(a->x + a->w, b->x + b->w);
  int y2 = MIN(a->y + a->h, b->y + b->h);

  if (x2 > x1 && y2 > y1) {
    dest->x = x1;
    dest->y = y1;
    dest->w = x2 - x1;
    dest->h = y2 - y1;
    return true;
  }

  /* No intersection */
  dest->x = 0;
  dest->y = 0;
  dest->w = 0;
  dest->h = 0;
  return false;
}
