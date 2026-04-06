#include "paging.h"
#include "../../drivers/serial.h"
#include "../../drivers/vbe.h"
#include "../include/heap.h"
#include <string.h>

#define PAGE_DIRECTORY_INDEX(addr) (((addr) >> 22) & 0x3FFU)
#define PAGE_TABLE_INDEX(addr) (((addr) >> 12) & 0x3FFU)
#define PAGE_ALIGN_DOWN(value) ((value) & ~(PAGE_SIZE - 1U))
#define PAGE_ALIGN_UP(value) (((value) + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U))
#define PAGING_STATIC_KERNEL_TABLES 12U
#define PAGING_FRAME_POOL_MAX_PAGES \
  (PAGING_SHARED_KERNEL_END / PAGE_SIZE)

static page_directory_t kernel_page_directory
    __attribute__((aligned(PAGE_SIZE)));
static page_table_t kernel_page_tables[PAGING_STATIC_KERNEL_TABLES]
    __attribute__((aligned(PAGE_SIZE)));
static size_t kernel_page_table_count = PAGING_STATIC_KERNEL_TABLES;
static int paging_initialized = 0;
static uintptr_t frame_pool_start = 0;
static uintptr_t frame_pool_end = 0;
static uint8_t frame_pool_used[PAGING_FRAME_POOL_MAX_PAGES];
static page_directory_t *current_page_directory = &kernel_page_directory;

static uintptr_t paging_frame_pool_page_count(void) {
  if (frame_pool_end <= frame_pool_start) {
    return 0;
  }
  return (frame_pool_end - frame_pool_start) / PAGE_SIZE;
}

static uintptr_t paging_read_cr3(void) {
  uintptr_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

static void paging_flush_tlb(uintptr_t address) {
  __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

static void paging_init_frame_pool(void) {
  uintptr_t heap_end = heap_get_end();

  frame_pool_start = PAGE_ALIGN_UP(heap_end);
  frame_pool_end = PAGING_SHARED_KERNEL_END;

  if (frame_pool_start >= frame_pool_end) {
    serial_print("[PAGING] Frame pool unavailable\n");
    for (;;) {
      __asm__ volatile("cli; hlt");
    }
  }

  memset(frame_pool_used, 0, sizeof(frame_pool_used));

  serial_print("[PAGING] Frame pool: 0x");
  serial_print_hex((uint32_t)frame_pool_start);
  serial_print(" - 0x");
  serial_print_hex((uint32_t)frame_pool_end);
  serial_print(" pages=");
  serial_print_dec((uint32_t)paging_frame_pool_page_count());
  serial_print("\n");
}

static page_table_t *paging_get_table(page_directory_t *directory,
                                      uint32_t directory_index) {
  uint32_t entry = directory->entries[directory_index];

  if ((entry & PAGE_PRESENT) == 0) {
    return NULL;
  }
  return (page_table_t *)(uintptr_t)(entry & PAGE_FRAME_MASK);
}

static page_table_t *paging_get_or_create_table(page_directory_t *directory,
                                                uint32_t directory_index,
                                                uint32_t flags) {
  page_table_t *table = paging_get_table(directory, directory_index);
  uintptr_t table_frame;
  uint32_t directory_flags = PAGE_PRESENT | PAGE_WRITABLE;

  if (table != NULL) {
    if (flags & PAGE_USER) {
      directory->entries[directory_index] |= PAGE_USER;
    }
    return table;
  }

  table_frame = page_frame_alloc();
  if (table_frame == 0) {
    return NULL;
  }

  table = (page_table_t *)(uintptr_t)table_frame;
  memset(table, 0, sizeof(*table));
  if (flags & PAGE_USER) {
    directory_flags |= PAGE_USER;
  }

  directory->entries[directory_index] =
      ((uint32_t)table_frame & PAGE_FRAME_MASK) | directory_flags;

  return table;
}

static void paging_install_static_kernel_table(uint32_t directory_index,
                                               page_table_t *table) {
  kernel_page_directory.entries[directory_index] =
      ((uint32_t)(uintptr_t)table & PAGE_FRAME_MASK) | PAGE_PRESENT |
      PAGE_WRITABLE;
}

static void paging_build_static_kernel_tables(void) {
  uint32_t physical_address = 0;
  uintptr_t framebuffer_start = (uintptr_t)vbe_get_framebuffer();
  uintptr_t framebuffer_page = PAGE_ALIGN_DOWN(framebuffer_start);

  memset(&kernel_page_directory, 0, sizeof(kernel_page_directory));
  memset(&kernel_page_tables, 0, sizeof(kernel_page_tables));

  for (uint32_t directory_index = 0; directory_index < 8U; ++directory_index) {
    page_table_t *table = &kernel_page_tables[directory_index];

    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
      table->entries[i] = physical_address | PAGE_PRESENT | PAGE_WRITABLE;
      physical_address += PAGE_SIZE;
    }

    paging_install_static_kernel_table(directory_index, table);
  }

  for (uint32_t table_index = 0; table_index < 4U; ++table_index) {
    page_table_t *table = &kernel_page_tables[8U + table_index];
    uint32_t directory_index =
        PAGE_DIRECTORY_INDEX((uint32_t)(framebuffer_page + table_index * 0x400000U));

    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
      uintptr_t physical = framebuffer_page + (table_index * 0x400000U) +
                           ((uintptr_t)i * PAGE_SIZE);
      table->entries[i] =
          ((uint32_t)physical & PAGE_FRAME_MASK) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    paging_install_static_kernel_table(directory_index, table);
  }
}

uintptr_t page_frame_alloc(void) {
  uintptr_t page_count = paging_frame_pool_page_count();

  for (uintptr_t i = 0; i < page_count; ++i) {
    if (frame_pool_used[i] == 0) {
      uintptr_t frame = frame_pool_start + (i * PAGE_SIZE);
      frame_pool_used[i] = 1;
      memset((void *)frame, 0, PAGE_SIZE);
      return frame;
    }
  }

  serial_print("[PAGING] Frame allocation failed\n");
  return 0;
}

void page_frame_free(uintptr_t frame) {
  uintptr_t index;

  if (frame < frame_pool_start || frame >= frame_pool_end) {
    return;
  }

  frame = PAGE_ALIGN_DOWN(frame);
  index = (frame - frame_pool_start) / PAGE_SIZE;
  if (index < PAGING_FRAME_POOL_MAX_PAGES) {
    frame_pool_used[index] = 0;
  }
}

void paging_enable(void) {
  uintptr_t directory_address = (uintptr_t)&kernel_page_directory;
  uint32_t cr0;

  __asm__ volatile("mov %0, %%cr3" : : "r"(directory_address) : "memory");
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= 0x80000000U;
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
  __asm__ volatile("jmp 1f\n1:" : : : "memory");
}

void paging_init(void) {
  if (paging_initialized) {
    return;
  }

  paging_initialized = 1;

  serial_print("[PAGING] Initializing...\n");

  paging_build_static_kernel_tables();
  paging_init_frame_pool();

  paging_enable();
  current_page_directory = &kernel_page_directory;

  serial_print("[PAGING] Page directory: 0x");
  serial_print_hex((uint32_t)(uintptr_t)&kernel_page_directory);
  serial_print("\n");
  serial_print("[PAGING] Page tables: 0x");
  serial_print_hex((uint32_t)(uintptr_t)&kernel_page_tables[0]);
  serial_print("\n");
  serial_print("[PAGING] Enabled successfully\n");
}

int paging_self_test(void) {
  address_space_t scratch;
  uintptr_t old_cr3 = paging_read_cr3();

  if (!paging_create_address_space(&scratch)) {
    serial_print("[PAGING] Self-test create failed\n");
    return 0;
  }

  paging_switch_directory(scratch.page_directory);
  serial_print("[PAGING] Self-test switch old=0x");
  serial_print_hex((uint32_t)old_cr3);
  serial_print(" new=0x");
  serial_print_hex((uint32_t)scratch.cr3);
  serial_print("\n");
  paging_switch_directory(&kernel_page_directory);
  paging_destroy_address_space(&scratch);
  return 1;
}

int paging_create_address_space(address_space_t *address_space) {
  uintptr_t directory_frame;

  if (address_space == NULL) {
    return 0;
  }

  directory_frame = page_frame_alloc();
  if (directory_frame == 0) {
    return 0;
  }

  address_space->page_directory = (page_directory_t *)(uintptr_t)directory_frame;
  memset(address_space->page_directory, 0, sizeof(page_directory_t));
  memcpy(address_space->page_directory->entries, kernel_page_directory.entries,
         sizeof(kernel_page_directory.entries));
  address_space->cr3 = directory_frame;
  return 1;
}

void paging_destroy_address_space(address_space_t *address_space) {
  page_directory_t *directory;

  if (address_space == NULL || address_space->page_directory == NULL ||
      address_space->page_directory == &kernel_page_directory) {
    return;
  }

  directory = address_space->page_directory;
  for (uint32_t directory_index = PAGE_DIRECTORY_INDEX(PAGING_USER_BASE);
       directory_index < PAGE_DIRECTORY_INDEX(PAGING_USER_LIMIT - 1U) + 1U;
       ++directory_index) {
    page_table_t *table = paging_get_table(directory, directory_index);

    if (table == NULL) {
      continue;
    }

    for (uint32_t table_index = 0; table_index < PAGE_TABLE_ENTRIES;
         ++table_index) {
      uint32_t entry = table->entries[table_index];
      if (entry & PAGE_PRESENT) {
        page_frame_free((uintptr_t)(entry & PAGE_FRAME_MASK));
      }
    }

    page_frame_free((uintptr_t)table);
    directory->entries[directory_index] = 0;
  }

  page_frame_free((uintptr_t)directory);
  address_space->page_directory = NULL;
  address_space->cr3 = 0;
}

int paging_map_page(page_directory_t *directory, uintptr_t virtual_address,
                    uintptr_t physical_address, uint32_t flags) {
  uint32_t directory_index;
  uint32_t table_index;
  page_table_t *table;

  if (directory == NULL) {
    return 0;
  }

  virtual_address = PAGE_ALIGN_DOWN(virtual_address);
  physical_address = PAGE_ALIGN_DOWN(physical_address);
  directory_index = PAGE_DIRECTORY_INDEX((uint32_t)virtual_address);
  table_index = PAGE_TABLE_INDEX((uint32_t)virtual_address);
  table = paging_get_or_create_table(directory, directory_index, flags);
  if (table == NULL) {
    return 0;
  }
  if (table->entries[table_index] & PAGE_PRESENT) {
    return 0;
  }

  table->entries[table_index] =
      ((uint32_t)physical_address & PAGE_FRAME_MASK) | PAGE_PRESENT |
      (flags & (PAGE_WRITABLE | PAGE_USER));
  if (directory == current_page_directory) {
    paging_flush_tlb(virtual_address);
  }
  return 1;
}

int paging_map_range(page_directory_t *directory, uintptr_t virtual_address,
                     uintptr_t physical_address, size_t length, uint32_t flags) {
  uintptr_t current_virtual;
  uintptr_t current_physical;
  uintptr_t end;

  if (length == 0) {
    return 1;
  }

  current_virtual = PAGE_ALIGN_DOWN(virtual_address);
  current_physical = PAGE_ALIGN_DOWN(physical_address);
  end = PAGE_ALIGN_UP(virtual_address + length);

  for (; current_virtual < end;
       current_virtual += PAGE_SIZE, current_physical += PAGE_SIZE) {
    if (!paging_map_page(directory, current_virtual, current_physical, flags)) {
      return 0;
    }
  }

  return 1;
}

void paging_unmap_page(page_directory_t *directory, uintptr_t virtual_address) {
  uint32_t directory_index;
  uint32_t table_index;
  page_table_t *table;

  if (directory == NULL) {
    return;
  }

  virtual_address = PAGE_ALIGN_DOWN(virtual_address);
  directory_index = PAGE_DIRECTORY_INDEX((uint32_t)virtual_address);
  table_index = PAGE_TABLE_INDEX((uint32_t)virtual_address);
  table = paging_get_table(directory, directory_index);
  if (table == NULL) {
    return;
  }

  table->entries[table_index] = 0;
  if (directory == current_page_directory) {
    paging_flush_tlb(virtual_address);
  }
}

void paging_switch_directory(page_directory_t *directory) {
  uintptr_t directory_address;

  if (directory == NULL || directory == current_page_directory) {
    return;
  }

  directory_address = (uintptr_t)directory;
  __asm__ volatile("mov %0, %%cr3" : : "r"(directory_address) : "memory");
  current_page_directory = directory;
}

int paging_is_user_range_mapped(page_directory_t *directory, uintptr_t address,
                                size_t length, int writable) {
  uintptr_t end;
  uintptr_t page;

  if (directory == NULL) {
    return 0;
  }

  if (length == 0) {
    return 1;
  }

  if (address < PAGING_USER_BASE || address >= PAGING_USER_LIMIT) {
    return 0;
  }

  if (address + length < address) {
    return 0;
  }

  end = address + length - 1U;
  if (end >= PAGING_USER_LIMIT) {
    return 0;
  }

  for (page = PAGE_ALIGN_DOWN(address); page <= PAGE_ALIGN_DOWN(end);
       page += PAGE_SIZE) {
    uint32_t directory_index = PAGE_DIRECTORY_INDEX((uint32_t)page);
    uint32_t table_index = PAGE_TABLE_INDEX((uint32_t)page);
    uint32_t directory_entry = directory->entries[directory_index];
    page_table_t *table;
    uint32_t page_entry;

    if ((directory_entry & (PAGE_PRESENT | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_USER)) {
      return 0;
    }

    table = (page_table_t *)(uintptr_t)(directory_entry & PAGE_FRAME_MASK);
    page_entry = table->entries[table_index];
    if ((page_entry & (PAGE_PRESENT | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_USER)) {
      return 0;
    }
    if (writable && (page_entry & PAGE_WRITABLE) == 0) {
      return 0;
    }
  }

  return 1;
}

page_directory_t *paging_get_directory(void) { return &kernel_page_directory; }

page_directory_t *paging_get_current_directory(void) {
  return current_page_directory;
}

uintptr_t paging_get_current_cr3(void) { return paging_read_cr3(); }

page_table_t *paging_get_table_pool(void) { return &kernel_page_tables[0]; }

size_t paging_get_table_count(void) { return kernel_page_table_count; }
