#include "font_cache.h"
#include "../../include/string.h"
#include "../include/heap.h"

#define CACHE_SIZE 512

static glyph_cache_entry_t cache[CACHE_SIZE];

void font_cache_init(void) { memset(cache, 0, sizeof(cache)); }

void font_cache_clear(void) {
  for (int i = 0; i < CACHE_SIZE; i++) {
    if (cache[i].used && cache[i].bitmap) {
      kfree(cache[i].bitmap);
    }
  }
  memset(cache, 0, sizeof(cache));
}

static uint32_t hash_key(cache_key_t key) {
  /* Simple hash */
  return (key ^ (key >> 12)) % CACHE_SIZE;
}

glyph_cache_entry_t *font_cache_get(uint16_t glyph_index, uint16_t size) {
  cache_key_t key = ((uint32_t)glyph_index << 16) | size;
  uint32_t start_idx = hash_key(key);
  uint32_t idx = start_idx;

  /* Linear probe */
  do {
    if (!cache[idx].used) {
      return NULL; // Not found
    }
    if (cache[idx].key == key) {
      return &cache[idx]; // Found
    }
    idx = (idx + 1) % CACHE_SIZE;
  } while (idx != start_idx);

  return NULL;
}

void font_cache_put(uint16_t glyph_index, uint16_t size, const uint8_t *bitmap,
                    int width, int height, int offset_x, int offset_y,
                    int advance) {
  cache_key_t key = ((uint32_t)glyph_index << 16) | size;
  uint32_t start_idx = hash_key(key);
  uint32_t idx = start_idx;

  /* Linear probe to find empty slot or replace (simple policy) */
  /* 1. Try to find empty slot */
  int empty_slot = -1;

  do {
    if (!cache[idx].used) {
      empty_slot = idx;
      break;
    }
    if (cache[idx].key == key) {
      /* Already exists? Update? For now just return. */
      return;
    }
    idx = (idx + 1) % CACHE_SIZE;
  } while (idx != start_idx);

  /* 2. If full, just overwrite the hashed slot (Direct Mapped fallback for
     collision) Wait, simple linear probe shouldn't just overwrite arbitrary
     unless full. If we didn't find empty slot, cache is FULL. In "Simple"
     implementation, let's just use the START_IDX if full, forcing an eviction
     of whatever hashed there. Actually, `idx` looped back to `start_idx`. Cache
     is full. Let's evict the start_idx.
  */

  if (empty_slot == -1) {
    // Force eviction at hash position
    empty_slot = start_idx;
    if (cache[empty_slot].used && cache[empty_slot].bitmap) {
      kfree(cache[empty_slot].bitmap);
      cache[empty_slot].used = false;
    }
  }

  /* Insert */
  glyph_cache_entry_t *e = &cache[empty_slot];
  e->key = key;
  e->used = true;
  e->width = width;
  e->height = height;
  e->offset_x = offset_x;
  e->offset_y = offset_y;
  e->advance = advance;

  /* Allocate and copy bitmap */
  /* Only if we have something to store */
  if (bitmap && width > 0 && height > 0) {
    int sz = width * height;
    e->bitmap = (uint8_t *)kalloc(sz);
    if (e->bitmap) {
      memcpy(e->bitmap, bitmap, sz);
    } else {
      e->used = false; // Allocation failed
    }
  } else {
    e->bitmap = NULL;
  }
}
