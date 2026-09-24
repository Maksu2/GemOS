#ifndef FONT_MEM_H
#define FONT_MEM_H

#include <stddef.h>

/* Memory of the font engine: blocks up to POOL_MAX_SIZE come from a pool
 * of their own (kernel/memory/pool.c), bigger ones from the heap. Always
 * free with font_free(). */
void font_mem_init(void);
void *font_alloc(size_t size);
void font_free(void *ptr);

#endif /* FONT_MEM_H */
