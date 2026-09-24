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

#endif /* HEAP_H */
