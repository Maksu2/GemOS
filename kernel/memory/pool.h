#ifndef POOL_H
#define POOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Pool for small allocations: size classes of 16..4096 bytes. Each class
 * carves 16 KB chunks from the pool's memory and keeps its free blocks on
 * a list, so many small, short-lived blocks (the font engine's) never
 * fragment the main heap. Blocks carry a header with a magic value, so a
 * double free is caught like in the heap.
 */
#define POOL_CLASS_COUNT 9
#define POOL_MAX_SIZE 4096U

typedef struct pool_block pool_block_t;

typedef struct {
  uint8_t *start;
  uint8_t *end;
  uint8_t *next_chunk; /* first byte not carved yet */
  pool_block_t *free_list[POOL_CLASS_COUNT];
  uint32_t in_use[POOL_CLASS_COUNT];
} small_pool_t;

void pool_init(small_pool_t *pool, void *memory, size_t size);
/* NULL if size > POOL_MAX_SIZE or the pool is exhausted */
void *pool_alloc(small_pool_t *pool, size_t size);
void pool_free(small_pool_t *pool, void *ptr);
int pool_contains(const small_pool_t *pool, const void *ptr);
/* blocks currently handed out */
uint32_t pool_in_use(const small_pool_t *pool);

#endif /* POOL_H */
