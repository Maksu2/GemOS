#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Physical memory plan from the E820 map (boot_info). The kernel reaches
 * physical memory only through the identity map of the first 32 MB
 * (PAGING_SHARED_KERNEL_END, where user space begins), so the heap and the
 * page frame pool are placed in usable RAM below that line. RAM above it is
 * reported but not used.
 */
typedef struct {
  uintptr_t heap_start;
  size_t heap_size;
  uintptr_t frames_start; /* frame pool: usable pages in [start, end) */
  uintptr_t frames_end;
  size_t frame_count;
} memory_layout_t;

/* Heap right after the kernel (at least heap_min bytes, at most
 * PMM_HEAP_MAX), frame pool in the usable memory after it. Returns 0 and
 * logs the numbers if there is not enough RAM. */
int pmm_plan(uintptr_t kernel_end, size_t heap_min, memory_layout_t *layout);

/* Non-zero if the whole 4 KB page at addr is usable RAM. */
int pmm_page_usable(uintptr_t addr);

#endif /* PMM_H */
