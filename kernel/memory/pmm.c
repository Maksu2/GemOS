#include "pmm.h"

#include "../../drivers/serial.h"
#include "../include/boot_info.h"
#include "paging.h"

#define PMM_HEAP_MAX (24U * 1024U * 1024U)
/* Page tables, page directories and user pages of all processes. */
#define PMM_FRAMES_MIN (2U * 1024U * 1024U)

static int pmm_entry_valid(const e820_entry_t *entry) {
  /* ACPI 3.0: bit 0 clear means "ignore this entry" */
  return entry->length != 0 && (entry->acpi & 1U) != 0;
}

int pmm_page_usable(uintptr_t addr) {
  uint64_t start = addr & ~(uint64_t)(PAGE_SIZE - 1U);
  uint64_t end = start + PAGE_SIZE;
  int usable = 0;

  for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
    const e820_entry_t *entry = &boot_info.e820[i];
    uint64_t entry_end = entry->base + entry->length;

    if (!pmm_entry_valid(entry)) {
      continue;
    }
    if (entry->type == E820_USABLE) {
      if (start >= entry->base && end <= entry_end) {
        usable = 1;
      }
    } else if (start < entry_end && entry->base < end) {
      return 0; /* reserved ranges win over overlapping usable ones */
    }
  }
  return usable;
}

static void pmm_print_kb(const char *label, uint64_t bytes) {
  serial_print(label);
  serial_print_dec((uint32_t)(bytes / 1024U));
  serial_print(" KB");
}

int pmm_plan(uintptr_t kernel_end, size_t heap_min, memory_layout_t *layout) {
  uintptr_t heap_start = (kernel_end + PAGE_SIZE - 1U) & PAGE_FRAME_MASK;
  uintptr_t contiguous_end = heap_start;
  uint64_t ram = 0;
  uint64_t ram_above = 0;
  size_t available;
  size_t heap_size;

  for (uint32_t i = 0; i < boot_info.e820_count; ++i) {
    const e820_entry_t *entry = &boot_info.e820[i];
    uint64_t entry_end = entry->base + entry->length;

    if (pmm_entry_valid(entry) && entry->type == E820_USABLE) {
      ram += entry->length;
      if (entry_end > PAGING_SHARED_KERNEL_END) {
        ram_above += entry_end - (entry->base > PAGING_SHARED_KERNEL_END
                                      ? entry->base
                                      : PAGING_SHARED_KERNEL_END);
      }
    }
  }

  /* The heap needs one contiguous block after the kernel. */
  while (contiguous_end < PAGING_SHARED_KERNEL_END &&
         pmm_page_usable(contiguous_end)) {
    contiguous_end += PAGE_SIZE;
  }
  available = contiguous_end - heap_start;
  heap_size = available > PMM_FRAMES_MIN ? available - PMM_FRAMES_MIN : 0;
  if (heap_size > PMM_HEAP_MAX) {
    heap_size = PMM_HEAP_MAX;
  }
  heap_size &= PAGE_FRAME_MASK;

  layout->heap_start = heap_start;
  layout->heap_size = heap_size;
  layout->frames_start = heap_start + heap_size;
  layout->frames_end = PAGING_SHARED_KERNEL_END;
  layout->frame_count = 0;
  for (uintptr_t page = layout->frames_start; page < layout->frames_end;
       page += PAGE_SIZE) {
    if (pmm_page_usable(page)) {
      layout->frame_count++;
    }
  }

  pmm_print_kb("[MEM] E820 usable RAM: ", ram);
  if (ram_above != 0) {
    pmm_print_kb(", not used above 32 MB: ", ram_above);
  }
  serial_print("\n");

  if (boot_info.e820_count == 0 || heap_size < heap_min ||
      layout->frame_count * PAGE_SIZE < PMM_FRAMES_MIN) {
    pmm_print_kb("[MEM] Not enough RAM: ", available);
    pmm_print_kb(" free after the kernel, need ", (uint64_t)heap_min +
                                                      PMM_FRAMES_MIN);
    pmm_print_kb(" (heap ", heap_min);
    pmm_print_kb(" + page frames ", PMM_FRAMES_MIN);
    serial_print(")\n");
    return 0;
  }

  serial_print("[MEM] Heap 0x");
  serial_print_hex((uint32_t)heap_start);
  pmm_print_kb(" ", heap_size);
  serial_print(", frames 0x");
  serial_print_hex((uint32_t)layout->frames_start);
  serial_print("-0x");
  serial_print_hex((uint32_t)layout->frames_end);
  serial_print(" (");
  serial_print_dec((uint32_t)layout->frame_count);
  serial_print(" pages)\n");
  return 1;
}
