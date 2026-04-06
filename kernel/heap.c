#include "include/heap.h"
#include "../drivers/serial.h"

/* Header for each memory block */
typedef struct block_header {
  struct block_header *next;
  size_t size; /* Size of the data block (excluding header) */
  uint8_t is_free;
} block_header_t;

static block_header_t *heap_start = NULL;
static uintptr_t heap_base = 0;
static size_t heap_total_size = 0;

void heap_init(uintptr_t start, size_t size) {
  /* Align start addres to 4 bytes */
  if (start & 3) {
    uintptr_t aligned_start = (start + 3) & ~((uintptr_t)3);
    size -= (size_t)(aligned_start - start);
    start = aligned_start;
  }

  heap_start = (block_header_t *)start;
  heap_base = start;
  heap_total_size = size;
  heap_start->next = NULL;
  heap_start->size = size - sizeof(block_header_t);
  heap_start->is_free = 1;

  serial_print("[HEAP] Initialized at 0x");
  serial_print_hex(start);
  serial_print(" with size: ");
  serial_print_dec(heap_start->size);
  serial_print(" bytes\n");
}

void *kalloc(size_t size) {
  if (size == 0)
    return NULL;

  /* Align size to 4 bytes */
  size = (size + 3) & ~3;

  block_header_t *current = heap_start;

  while (current) {
    if (current->is_free && current->size >= size) {
      /* Found a fit */

      /* Check if we can split */
      if (current->size >= size + sizeof(block_header_t) + 4) {
        block_header_t *new_block =
            (block_header_t *)((uintptr_t)current + sizeof(block_header_t) +
                               size);
        new_block->size = current->size - size - sizeof(block_header_t);
        new_block->is_free = 1;
        new_block->next = current->next;

        current->size = size;
        current->next = new_block;
      }

      current->is_free = 0;

      /* Return pointer to data after header */
      return (void *)((uintptr_t)current + sizeof(block_header_t));
    }
    current = current->next;
  }

  serial_print("[HEAP] Alloc failed for size: ");
  serial_print_dec(size);
  serial_print("\n");
  return NULL; /* Out of memory */
}

void kfree(void *ptr) {
  if (!ptr)
    return;

  /* Get header from data pointer */
  block_header_t *header =
      (block_header_t *)((uintptr_t)ptr - sizeof(block_header_t));
  header->is_free = 1;

  /* Coalesce with next block if free */
  if (header->next && header->next->is_free) {
    header->size += sizeof(block_header_t) + header->next->size;
    header->next = header->next->next;
  }

  /* Coalesce with prev block not easily possible with singly linked list
     without traversing from start. For simplicity/speed in this phase,
     we only coalesce forward. Full coalescence would require prev pointers.
     This is acceptable for Phase 3.1. */
}

uintptr_t heap_get_start(void) { return heap_base; }

uintptr_t heap_get_end(void) { return heap_base + heap_total_size; }

size_t heap_get_size(void) { return heap_total_size; }
