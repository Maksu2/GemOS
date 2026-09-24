/*
 * Kernel heap: first fit over the blocks in address order.
 *
 * Every block starts with a 32-byte header (magic, requested size,
 * capacity, neighbours) and the payload is followed by a canary. kfree()
 * checks both and merges the block with free neighbours on both sides.
 * Corruption (bad magic, a canary overwritten past the end of a block,
 * freeing twice or a pointer the heap never handed out) stops the kernel,
 * except in report mode, which the self-test uses.
 *
 * Only task code allocates (never interrupt handlers), and tasks do not
 * preempt each other in the kernel, so there is no lock.
 */
#include "include/heap.h"
#include "../drivers/serial.h"

#define HEAP_ALIGN 16U
#define HEAP_MAGIC_USED 0x45535548U /* "HUSE" */
#define HEAP_MAGIC_FREE 0x45524648U /* "HFRE" */
#define HEAP_CANARY 0xC0DEDBADU
#define HEAP_CANARY_SIZE 4U
#define HEAP_MIN_SPLIT HEAP_ALIGN /* smallest payload worth a new block */

typedef struct heap_block {
  uint32_t magic;
  uint32_t size;     /* bytes the caller asked for (used blocks) */
  uint32_t capacity; /* payload bytes that belong to the block */
  struct heap_block *prev; /* neighbours in address order */
  struct heap_block *next;
  uint32_t reserved[3]; /* keeps payloads 16-byte aligned */
} heap_block_t;

_Static_assert(sizeof(heap_block_t) == 32, "heap header must be 32 bytes");

static heap_block_t *heap_first = NULL;
static uintptr_t heap_start = 0;
static uintptr_t heap_end = 0;
static int heap_panic_on_error = 1;
static uint32_t heap_errors = 0;
static const char *heap_last_what = NULL;

static uint8_t *heap_payload(heap_block_t *block) {
  return (uint8_t *)block + sizeof(heap_block_t);
}

static void heap_write_canary(heap_block_t *block) {
  uint8_t *tail = heap_payload(block) + block->size;
  uint32_t canary = HEAP_CANARY;

  for (uint32_t i = 0; i < HEAP_CANARY_SIZE; ++i) {
    tail[i] = (uint8_t)(canary >> (8 * i));
  }
}

static int heap_canary_ok(heap_block_t *block) {
  const uint8_t *tail = heap_payload(block) + block->size;
  uint32_t canary = 0;

  for (uint32_t i = 0; i < HEAP_CANARY_SIZE; ++i) {
    canary |= (uint32_t)tail[i] << (8 * i);
  }
  return canary == HEAP_CANARY;
}

void heap_report_corruption(const char *what, const void *ptr) {
  heap_errors++;
  heap_last_what = what;
  serial_print("[HEAP] ");
  serial_print(what);
  serial_print(" at 0x");
  serial_print_hex((uint32_t)(uintptr_t)ptr);
  serial_print("\n");
  if (heap_panic_on_error) {
    serial_print("[PANIC] Heap corruption. System Halted.\n");
    for (;;) {
      __asm__ volatile("cli; hlt");
    }
  }
}

static int heap_block_valid(const heap_block_t *block) {
  uintptr_t address = (uintptr_t)block;

  return address >= heap_start && address + sizeof(heap_block_t) <= heap_end &&
         (address & (HEAP_ALIGN - 1U)) == 0 &&
         (block->magic == HEAP_MAGIC_USED || block->magic == HEAP_MAGIC_FREE);
}

void heap_init(uintptr_t start, size_t size) {
  uintptr_t aligned = (start + HEAP_ALIGN - 1U) & ~(uintptr_t)(HEAP_ALIGN - 1U);

  size -= (size_t)(aligned - start);
  size &= ~(size_t)(HEAP_ALIGN - 1U);

  heap_start = aligned;
  heap_end = aligned + size;
  heap_first = (heap_block_t *)aligned;
  heap_first->magic = HEAP_MAGIC_FREE;
  heap_first->size = 0;
  heap_first->capacity = (uint32_t)(size - sizeof(heap_block_t));
  heap_first->prev = NULL;
  heap_first->next = NULL;

  serial_print("[HEAP] Initialized at 0x");
  serial_print_hex((uint32_t)aligned);
  serial_print(" with size: ");
  serial_print_dec(heap_first->capacity);
  serial_print(" bytes\n");
}

void *kalloc(size_t size) {
  uint32_t need;

  if (size == 0 || size > heap_end - heap_start) {
    return NULL;
  }
  need = ((uint32_t)size + HEAP_CANARY_SIZE + HEAP_ALIGN - 1U) &
         ~(HEAP_ALIGN - 1U);

  for (heap_block_t *block = heap_first; block != NULL; block = block->next) {
    if (!heap_block_valid(block)) {
      heap_report_corruption("corrupted block header", block);
      return NULL;
    }
    if (block->magic != HEAP_MAGIC_FREE || block->capacity < need) {
      continue;
    }

    if (block->capacity >= need + sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
      heap_block_t *rest = (heap_block_t *)(heap_payload(block) + need);

      rest->magic = HEAP_MAGIC_FREE;
      rest->size = 0;
      rest->capacity = block->capacity - need - (uint32_t)sizeof(heap_block_t);
      rest->prev = block;
      rest->next = block->next;
      if (rest->next != NULL) {
        rest->next->prev = rest;
      }
      block->next = rest;
      block->capacity = need;
    }

    block->magic = HEAP_MAGIC_USED;
    block->size = (uint32_t)size;
    heap_write_canary(block);
    return heap_payload(block);
  }

  serial_print("[HEAP] Alloc failed for size: ");
  serial_print_dec((uint32_t)size);
  serial_print("\n");
  return NULL;
}

/* Merge b, the free block right after a, into a. */
static void heap_merge(heap_block_t *a, heap_block_t *b) {
  a->capacity += (uint32_t)sizeof(heap_block_t) + b->capacity;
  a->next = b->next;
  if (b->next != NULL) {
    b->next->prev = a;
  }
  b->magic = 0; /* a stale pointer to b no longer looks like a block */
}

void kfree(void *ptr) {
  heap_block_t *block;

  if (ptr == NULL) {
    return;
  }
  block = (heap_block_t *)((uintptr_t)ptr - sizeof(heap_block_t));

  if ((uintptr_t)ptr < heap_start + sizeof(heap_block_t) ||
      (uintptr_t)ptr >= heap_end || ((uintptr_t)ptr & (HEAP_ALIGN - 1U))) {
    heap_report_corruption("kfree of a pointer outside the heap", ptr);
    return;
  }
  if (block->magic == HEAP_MAGIC_FREE) {
    heap_report_corruption("double free", ptr);
    return;
  }
  if (!heap_block_valid(block)) {
    heap_report_corruption("kfree with a corrupted block header", ptr);
    return;
  }
  if (!heap_canary_ok(block)) {
    heap_report_corruption("write past the end of a block", ptr);
    return;
  }

  block->magic = HEAP_MAGIC_FREE;
  block->size = 0;

  if (block->next != NULL) {
    if (!heap_block_valid(block->next)) {
      heap_report_corruption("corrupted block header", block->next);
      return;
    }
    if (block->next->magic == HEAP_MAGIC_FREE) {
      heap_merge(block, block->next);
    }
  }
  if (block->prev != NULL) {
    if (!heap_block_valid(block->prev)) {
      heap_report_corruption("corrupted block header", block->prev);
      return;
    }
    if (block->prev->magic == HEAP_MAGIC_FREE) {
      heap_merge(block->prev, block);
    }
  }
}

void heap_get_stats(heap_stats_t *stats) {
  stats->used_bytes = 0;
  stats->free_bytes = 0;
  stats->largest_free = 0;
  stats->used_blocks = 0;
  stats->free_blocks = 0;

  for (heap_block_t *block = heap_first; block != NULL; block = block->next) {
    if (!heap_block_valid(block)) {
      break;
    }
    if (block->magic == HEAP_MAGIC_USED) {
      stats->used_bytes += block->capacity;
      stats->used_blocks++;
    } else {
      stats->free_bytes += block->capacity;
      stats->free_blocks++;
      if (block->capacity > stats->largest_free) {
        stats->largest_free = block->capacity;
      }
    }
  }
}

uint32_t heap_set_report_mode(int report_only) {
  heap_panic_on_error = !report_only;
  return heap_errors;
}

const char *heap_last_error(void) { return heap_last_what; }
