#include "font_mem.h"

#include "../../drivers/serial.h"
#include "../include/heap.h"
#include "../memory/pool.h"

/* A full glyph cache (512 bitmaps) at 12 px on a 2x screen needs about
 * 300 KB; outline buffers are freed right after each glyph. */
#define FONT_POOL_SIZE (1024U * 1024U)

static small_pool_t font_pool;
static int font_pool_ready;

void font_mem_init(void) {
  void *memory = kalloc(FONT_POOL_SIZE);

  if (memory == NULL) {
    serial_print("[FONT] No memory for the glyph pool, using the heap\n");
    return;
  }
  pool_init(&font_pool, memory, FONT_POOL_SIZE);
  font_pool_ready = 1;
}

void *font_alloc(size_t size) {
  void *ptr = font_pool_ready ? pool_alloc(&font_pool, size) : NULL;

  return ptr != NULL ? ptr : kalloc(size);
}

void font_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  if (font_pool_ready && pool_contains(&font_pool, ptr)) {
    pool_free(&font_pool, ptr);
  } else {
    kfree(ptr);
  }
}
