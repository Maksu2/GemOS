#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include <stdint.h>

/* Initialize heap with start address and size */
void heap_init(uintptr_t start, size_t size);

/* Allocate memory */
void *kalloc(size_t size);

/* Free memory */
void kfree(void *ptr);

/* Heap metadata */
uintptr_t heap_get_start(void);
uintptr_t heap_get_end(void);
size_t heap_get_size(void);

#endif /* HEAP_H */
