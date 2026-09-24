#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

/*
 * Boot info block built by stage 2 (boot/stage2/loader.asm). EBX points to
 * it when the kernel starts; kernel_main copies it to boot_info before
 * anything else runs.
 */
#define BOOT_INFO_MAGIC 0x424D4547U /* "GEMB" */
#define BOOT_INFO_E820_MAX 32U

#define E820_USABLE 1U

typedef struct {
  uint64_t base;
  uint64_t length;
  uint32_t type;
  uint32_t acpi;
} __attribute__((packed)) e820_entry_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t boot_drive;   /* BIOS drive number: 0x00 floppy, 0x80 hard disk */
  uint32_t kernel_bytes; /* bytes of kernel image copied to 1 MB */
  uint32_t vbe_mode;     /* VBE mode number that was set */
  uint32_t e820_count;
  e820_entry_t e820[BOOT_INFO_E820_MAX];
  uint8_t vbe_mode_info[256]; /* VBE ModeInfoBlock of vbe_mode */
} __attribute__((packed)) boot_info_t;

/* The kernel's copy */
extern boot_info_t boot_info;

#endif /* BOOT_INFO_H */
