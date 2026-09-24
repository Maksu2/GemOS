#include "pool.h"

#include "../include/heap.h"

#define POOL_CHUNK_SIZE (16U * 1024U)
#define POOL_MAGIC_USED 0x4C4F4F50U /* "POOL" */
#define POOL_MAGIC_FREE 0x45455246U /* "FREE" */

struct pool_block {
  uint32_t magic;
  uint32_t class_index;
  pool_block_t *next_free;
  uint32_t reserved; /* 16-byte header: payloads stay 16-byte aligned */
};

static uint32_t pool_class_size(int class_index) {
  return 16U << class_index; /* 16, 32, ..., 4096 */
}

static int pool_class_for(size_t size) {
  for (int i = 0; i < POOL_CLASS_COUNT; ++i) {
    if (size <= pool_class_size(i)) {
      return i;
    }
  }
  return -1;
}

void pool_init(small_pool_t *pool, void *memory, size_t size) {
  uintptr_t start = ((uintptr_t)memory + 15U) & ~(uintptr_t)15U;

  pool->start = (uint8_t *)start;
  pool->end = (uint8_t *)memory + size;
  pool->next_chunk = pool->start;
  for (int i = 0; i < POOL_CLASS_COUNT; ++i) {
    pool->free_list[i] = NULL;
    pool->in_use[i] = 0;
  }
}

/* Cut a new chunk into blocks of one class. */
static int pool_refill(small_pool_t *pool, int class_index) {
  uint32_t block_size = (uint32_t)sizeof(pool_block_t) +
                        pool_class_size(class_index);
  uint8_t *chunk = pool->next_chunk;

  if ((size_t)(pool->end - chunk) < POOL_CHUNK_SIZE) {
    return 0;
  }
  pool->next_chunk += POOL_CHUNK_SIZE;

  for (uint32_t offset = 0; offset + block_size <= POOL_CHUNK_SIZE;
       offset += block_size) {
    pool_block_t *block = (pool_block_t *)(chunk + offset);

    block->magic = POOL_MAGIC_FREE;
    block->class_index = (uint32_t)class_index;
    block->next_free = pool->free_list[class_index];
    pool->free_list[class_index] = block;
  }
  return 1;
}

void *pool_alloc(small_pool_t *pool, size_t size) {
  int class_index = pool_class_for(size);
  pool_block_t *block;

  if (size == 0 || class_index < 0) {
    return NULL;
  }
  if (pool->free_list[class_index] == NULL &&
      !pool_refill(pool, class_index)) {
    return NULL;
  }

  block = pool->free_list[class_index];
  pool->free_list[class_index] = block->next_free;
  block->magic = POOL_MAGIC_USED;
  block->next_free = NULL;
  pool->in_use[class_index]++;
  return (uint8_t *)block + sizeof(pool_block_t);
}

int pool_contains(const small_pool_t *pool, const void *ptr) {
  return (const uint8_t *)ptr >= pool->start &&
         (const uint8_t *)ptr < pool->next_chunk;
}

void pool_free(small_pool_t *pool, void *ptr) {
  pool_block_t *block;

  if (ptr == NULL) {
    return;
  }
  block = (pool_block_t *)((uint8_t *)ptr - sizeof(pool_block_t));
  if (block->magic == POOL_MAGIC_FREE) {
    heap_report_corruption("double free in the small-object pool", ptr);
    return;
  }
  if (block->magic != POOL_MAGIC_USED ||
      block->class_index >= POOL_CLASS_COUNT) {
    heap_report_corruption("free with a corrupted pool block header", ptr);
    return;
  }

  block->magic = POOL_MAGIC_FREE;
  block->next_free = pool->free_list[block->class_index];
  pool->free_list[block->class_index] = block;
  pool->in_use[block->class_index]--;
}

uint32_t pool_in_use(const small_pool_t *pool) {
  uint32_t total = 0;

  for (int i = 0; i < POOL_CLASS_COUNT; ++i) {
    total += pool->in_use[i];
  }
  return total;
}
