#ifndef PAGING_H
#define PAGING_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_PRESENT 0x1U
#define PAGE_WRITABLE 0x2U
#define PAGE_USER 0x4U
#define PAGE_SIZE 0x1000U
#define PAGE_TABLE_ENTRIES 1024U
#define PAGE_DIRECTORY_ENTRIES 1024U
#define PAGE_FRAME_MASK 0xFFFFF000U

#define PAGING_SHARED_KERNEL_END 0x02000000U
#define PAGING_USER_BASE 0x02000000U
#define PAGING_USER_LIMIT 0x08000000U
#define PAGING_USER_STACK_TOP 0x07FFF000U
#define PAGING_USER_STACK_PAGES 2U
#define PAGING_FRAMEBUFFER_REGION_SIZE (16U * 1024U * 1024U)

typedef struct {
  uint32_t entries[PAGE_TABLE_ENTRIES];
} page_table_t;

typedef struct {
  uint32_t entries[PAGE_DIRECTORY_ENTRIES];
} page_directory_t;

typedef struct {
  page_directory_t *page_directory;
  uintptr_t cr3;
} address_space_t;

void paging_init(void);
void paging_enable(void);
int paging_self_test(void);

int paging_create_address_space(address_space_t *address_space);
void paging_destroy_address_space(address_space_t *address_space);
int paging_map_page(page_directory_t *directory, uintptr_t virtual_address,
                    uintptr_t physical_address, uint32_t flags);
int paging_map_range(page_directory_t *directory, uintptr_t virtual_address,
                     uintptr_t physical_address, size_t length, uint32_t flags);
int paging_update_page_flags(page_directory_t *directory, uintptr_t virtual_address,
                             uint32_t flags);
void paging_unmap_page(page_directory_t *directory, uintptr_t virtual_address);
void paging_switch_directory(page_directory_t *directory);
int paging_is_user_range_mapped(page_directory_t *directory, uintptr_t address,
                                size_t length, int writable);

uintptr_t page_frame_alloc(void);
void page_frame_free(uintptr_t frame);

page_directory_t *paging_get_directory(void);
page_directory_t *paging_get_current_directory(void);
uintptr_t paging_get_current_cr3(void);
page_table_t *paging_get_table_pool(void);
size_t paging_get_table_count(void);

#endif /* PAGING_H */
