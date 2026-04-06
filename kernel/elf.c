#include "elf.h"

#include "process.h"

#include "../drivers/serial.h"
#include "memory/paging.h"
#include <string.h>

#define ELF_IDENT_SIZE 16
#define ELF_MAGIC0 0x7FU
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS_32 1U
#define ELF_DATA_LSB 1U
#define ELF_TYPE_EXEC 2U
#define ELF_MACHINE_386 3U
#define ELF_VERSION_CURRENT 1U
#define ELF_PHDR_TYPE_LOAD 1U
#define ELF_PHDR_FLAG_WRITE 0x2U
#define ELF_MAX_LOAD_SEGMENTS 2U

typedef struct {
  uint8_t e_ident[ELF_IDENT_SIZE];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
  uint32_t p_type;
  uint32_t p_offset;
  uint32_t p_vaddr;
  uint32_t p_paddr;
  uint32_t p_filesz;
  uint32_t p_memsz;
  uint32_t p_flags;
  uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

static uint32_t elf_save_and_disable_interrupts(void) {
  uint32_t eflags;
  __asm__ volatile("pushfl; popl %0; cli" : "=r"(eflags) : : "memory");
  return eflags;
}

static void elf_restore_interrupts(uint32_t eflags) {
  if (eflags & 0x200U) {
    __asm__ volatile("sti");
  }
}

static int elf_validate_header(const elf32_ehdr_t *header, size_t image_size) {
  if (header == NULL || image_size < sizeof(*header)) {
    return 0;
  }

  if (header->e_ident[0] != ELF_MAGIC0 || header->e_ident[1] != ELF_MAGIC1 ||
      header->e_ident[2] != ELF_MAGIC2 || header->e_ident[3] != ELF_MAGIC3) {
    return 0;
  }
  if (header->e_ident[4] != ELF_CLASS_32 || header->e_ident[5] != ELF_DATA_LSB) {
    return 0;
  }
  if (header->e_type != ELF_TYPE_EXEC || header->e_machine != ELF_MACHINE_386 ||
      header->e_version != ELF_VERSION_CURRENT) {
    return 0;
  }
  if (header->e_phentsize != sizeof(elf32_phdr_t) || header->e_phnum == 0 ||
      header->e_phnum > ELF_MAX_LOAD_SEGMENTS) {
    return 0;
  }
  if ((size_t)header->e_phoff + ((size_t)header->e_phnum * sizeof(elf32_phdr_t)) >
      image_size) {
    return 0;
  }

  return 1;
}

static int elf_validate_segment(const elf32_phdr_t *segment, size_t image_size,
                                uintptr_t *lowest_base,
                                uintptr_t *highest_end) {
  uintptr_t stack_bottom =
      PAGING_USER_STACK_TOP - (PAGING_USER_STACK_PAGES * PAGE_SIZE);
  uintptr_t segment_start;
  uintptr_t segment_end;

  if (segment->p_type != ELF_PHDR_TYPE_LOAD) {
    return 1;
  }
  if (segment->p_memsz < segment->p_filesz) {
    return 0;
  }
  if ((size_t)segment->p_offset + segment->p_filesz > image_size) {
    return 0;
  }
  if (segment->p_vaddr < PAGING_USER_BASE) {
    return 0;
  }
  if ((uintptr_t)segment->p_vaddr + (uintptr_t)segment->p_memsz <
      (uintptr_t)segment->p_vaddr) {
    return 0;
  }

  segment_start = segment->p_vaddr;
  segment_end = segment->p_vaddr + segment->p_memsz;
  if (segment_end > stack_bottom || segment_end > PAGING_USER_LIMIT) {
    return 0;
  }

  if (*lowest_base == 0 || segment_start < *lowest_base) {
    *lowest_base = segment_start;
  }
  if (segment_end > *highest_end) {
    *highest_end = segment_end;
  }

  return 1;
}

static int elf_map_segment_pages(process_t *process, const elf32_phdr_t *segment) {
  uintptr_t segment_base = segment->p_vaddr & ~(PAGE_SIZE - 1U);
  uintptr_t segment_end =
      (segment->p_vaddr + segment->p_memsz + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
  uint32_t flags = PAGE_PRESENT | PAGE_USER;

  if (segment->p_flags & ELF_PHDR_FLAG_WRITE) {
    flags |= PAGE_WRITABLE;
  }

  for (uintptr_t address = segment_base; address < segment_end;
       address += PAGE_SIZE) {
    uintptr_t frame = page_frame_alloc();
    if (frame == 0) {
      return 0;
    }
    if (!paging_map_page(process->as.page_directory, address, frame, flags)) {
      page_frame_free(frame);
      return 0;
    }
  }

  return 1;
}

int elf_load_into_process(process_t *process, const uint8_t *image,
                          size_t image_size) {
  const elf32_ehdr_t *header = (const elf32_ehdr_t *)image;
  const elf32_phdr_t *segments;
  uintptr_t lowest_base = 0;
  uintptr_t highest_end = 0;
  uint32_t interrupt_state;

  serial_print("[ELF] Enter load\n");
  if (process == NULL || image == NULL) {
    serial_print("[ELF] Null input\n");
    return 0;
  }
  serial_print("[ELF] Raw magic=");
  serial_print_hex((uint32_t)header->e_ident[0]);
  serial_print_hex((uint32_t)header->e_ident[1]);
  serial_print_hex((uint32_t)header->e_ident[2]);
  serial_print_hex((uint32_t)header->e_ident[3]);
  serial_print(" type=");
  serial_print_hex(header->e_type);
  serial_print(" machine=");
  serial_print_hex(header->e_machine);
  serial_print(" phoff=");
  serial_print_hex(header->e_phoff);
  serial_print(" phnum=");
  serial_print_hex(header->e_phnum);
  serial_print(" phentsize=");
  serial_print_hex(header->e_phentsize);
  serial_print("\n");
  if (!elf_validate_header(header, image_size)) {
    serial_print("[ELF] Header invalid\n");
    return 0;
  }
  serial_print("[ELF] Header OK\n");

  segments = (const elf32_phdr_t *)(image + header->e_phoff);
  for (uint16_t i = 0; i < header->e_phnum; ++i) {
    if (!elf_validate_segment(&segments[i], image_size, &lowest_base, &highest_end)) {
      serial_print("[ELF] Segment validation failed\n");
      return 0;
    }
  }
  serial_print("[ELF] Segments OK\n");

  if (header->e_entry < lowest_base || header->e_entry >= highest_end) {
    serial_print("[ELF] Entry out of range\n");
    return 0;
  }

  for (uint16_t i = 0; i < header->e_phnum; ++i) {
    if (segments[i].p_type == ELF_PHDR_TYPE_LOAD &&
        !elf_map_segment_pages(process, &segments[i])) {
      serial_print("[ELF] Failed to map PT_LOAD\n");
      return 0;
    }
  }
  serial_print("[ELF] PT_LOAD pages mapped\n");

  process->user_stack_top = PAGING_USER_STACK_TOP;
  process->user_stack_bottom =
      PAGING_USER_STACK_TOP - (PAGING_USER_STACK_PAGES * PAGE_SIZE);
  for (uintptr_t address = process->user_stack_bottom;
       address < process->user_stack_top; address += PAGE_SIZE) {
    uintptr_t frame = page_frame_alloc();
    if (frame == 0) {
      return 0;
    }
    if (!paging_map_page(process->as.page_directory, address, frame,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
      page_frame_free(frame);
      return 0;
    }
  }
  serial_print("[ELF] Stack mapped\n");

  interrupt_state = elf_save_and_disable_interrupts();
  serial_print("[ELF] Switching to process CR3\n");
  paging_switch_directory(process->as.page_directory);
  for (uint16_t i = 0; i < header->e_phnum; ++i) {
    const elf32_phdr_t *segment = &segments[i];
    uintptr_t segment_base;
    size_t segment_span;

    if (segment->p_type != ELF_PHDR_TYPE_LOAD) {
      continue;
    }

    segment_base = segment->p_vaddr & ~(PAGE_SIZE - 1U);
    segment_span = (size_t)(((segment->p_vaddr + segment->p_memsz + PAGE_SIZE - 1U) &
                             ~(PAGE_SIZE - 1U)) -
                            segment_base);
    memset((void *)segment_base, 0, segment_span);
    memcpy((void *)(uintptr_t)segment->p_vaddr, image + segment->p_offset,
           segment->p_filesz);
  }
  paging_switch_directory(paging_get_directory());
  serial_print("[ELF] Restored kernel CR3\n");
  elf_restore_interrupts(interrupt_state);

  process->entry_eip = header->e_entry;
  process->image_base = lowest_base;
  process->image_end = highest_end;

  serial_print("[ELF] Loaded: entry=0x");
  serial_print_hex((uint32_t)process->entry_eip);
  serial_print(" base=0x");
  serial_print_hex((uint32_t)process->image_base);
  serial_print(" end=0x");
  serial_print_hex((uint32_t)process->image_end);
  serial_print("\n");
  return 1;
}
