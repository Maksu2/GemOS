#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

/* Initialize heap with start address and size */
void heap_init(uintptr_t start, size_t size);

/* Allocate memory (16-byte aligned) */
void *kalloc(size_t size);

/* Free memory. Double frees, overwritten headers and writes past the end of
 * the block are detected and stop the kernel. */
void kfree(void *ptr);

typedef struct {
  uint32_t used_bytes;
  uint32_t free_bytes;
  uint32_t largest_free;
  uint32_t used_blocks;
  uint32_t free_blocks;
} heap_stats_t;

void heap_get_stats(heap_stats_t *stats);

/* Report heap corruption: stops the kernel (or counts it, see below). The
 * small-object pool reports through here as well. */
void heap_report_corruption(const char *what, const void *ptr);

/* Self-test only: with report_only set, heap corruption is logged and
 * counted instead of stopping the kernel. Returns the number of errors
 * seen so far. */
uint32_t heap_set_report_mode(int report_only);
/* Self-test only: what the last report was about, NULL before the first. */
const char *heap_last_error(void);

#endif /* HEAP_H */
