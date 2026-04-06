/**
 * GemFS v0 - Implementation
 *
 * RAM-based file system with 16 slots, 8KB max per file.
 */

#include "gemfs.h"
#include "../../drivers/serial.h"

/**
 * GemFS v1 - Persistent
 *
 * Disk Layout:
 * LBA 1000: File Table (16 entries * 32 bytes = 512 bytes)
 *   - Entry: Name (24), Size (4), StartLBA (4)
 * LBA 1001+: Data Blocks (Fixed 16 sectors / 8KB per file)
 */

#include "../../drivers/ata.h"
#include "../../drivers/serial.h"
#include "gemfs.h"

#define GEMFS_START_LBA 1
#define GEMFS_FILE_SECTORS 16 /* 8KB */
/* GEMFS_MAX_FILES defined in gemfs.h */
/* GEMFS_MAX_FILES defined in gemfs.h */
#define GEMFS_ENTRY_SIZE 32

/* In-memory cache of file table */
/* MUST MATCH 32 BYTES ON DISK */
typedef struct {
  char name[20];       /* 20 bytes */
  uint32_t size;       /* 4 bytes */
  uint32_t start_lba;  /* 4 bytes */
  uint8_t type;        /* 1 byte (0=File, 1=Dir) */
  int8_t parent_idx;   /* 1 byte (-1=Root) */
  uint8_t reserved[2]; /* 2 bytes padding */
} __attribute__((packed)) gemfs_entry_t;

/* Static assertion to ensure size is 32 */
/* _Static_assert(sizeof(gemfs_entry_t) == 32, "GemFS Entry size mismatch"); */

static gemfs_entry_t file_table[GEMFS_MAX_FILES];
static uint8_t sector_buf[512]; /* Temp buffer */

/* Helper: String utils */
static int gemfs_strcmp(const char *a, const char *b) {
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

static void gemfs_strcpy(char *dst, const char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void gemfs_memset(void *ptr, int val, int size) {
  uint8_t *p = (uint8_t *)ptr;
  for (int i = 0; i < size; i++)
    p[i] = (uint8_t)val;
}

static void gemfs_sync_table(void) {
  /* Write table to LBA 1000 */
  /* 64 files * 32 bytes = 2048 bytes = 4 sectors */
  for (int i = 0; i < 4; i++) {
    ata_write_sector(GEMFS_START_LBA + i, (uint8_t *)file_table + (i * 512));
  }
}

void gemfs_init(void) {
  ata_init();

  /* Read LBA 1000-1003 (Table) */
  for (int i = 0; i < 4; i++) {
    ata_read_sector(GEMFS_START_LBA + i, (uint8_t *)file_table + (i * 512));
  }

  serial_print("[GemFS] v2 Mounted. Cache loaded.\n");
}

/* Core Find: Parent + Name */
int gemfs_find_in_dir(int parent_id, const char *name) {
  for (int i = 0; i < GEMFS_MAX_FILES; i++) {
    if (file_table[i].name[0] != '\0' &&
        file_table[i].parent_idx == (int8_t)parent_id &&
        gemfs_strcmp(file_table[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

/* Legacy Find: Searches ROOT */
int gemfs_find(const char *name) { return gemfs_find_in_dir(-1, name); }

static int gemfs_allocate_slot(void) {
  for (int i = 0; i < GEMFS_MAX_FILES; i++) {
    if (file_table[i].name[0] == '\0') {
      return i;
    }
  }
  return -1;
}

int gemfs_create_file(int parent_id, const char *name) {
  if (gemfs_find_in_dir(parent_id, name) >= 0)
    return -1;

  int i = gemfs_allocate_slot();
  if (i < 0)
    return -1;

  gemfs_strcpy(file_table[i].name, name, 20);
  file_table[i].size = 0;
  /* Calculate Fixed LBA: Start + 4 (Table) + i * 16 */
  file_table[i].start_lba = GEMFS_START_LBA + 4 + (i * GEMFS_FILE_SECTORS);
  file_table[i].type = GEMFS_TYPE_FILE;
  file_table[i].parent_idx = (int8_t)parent_id;

  gemfs_sync_table();
  serial_print("[GemFS] Created File: ");
  serial_print(name);
  serial_print("\n");
  return i;
}

int gemfs_create_dir(int parent_id, const char *name) {
  if (gemfs_find_in_dir(parent_id, name) >= 0)
    return -1;

  int i = gemfs_allocate_slot();
  if (i < 0)
    return -1;

  gemfs_strcpy(file_table[i].name, name, 20);
  file_table[i].size = 0;
  file_table[i].start_lba = 0; /* Dirs don't have data blocks yet */
  file_table[i].type = GEMFS_TYPE_DIR;
  file_table[i].parent_idx = (int8_t)parent_id;

  gemfs_sync_table();
  serial_print("[GemFS] Created Dir: ");
  serial_print(name);
  serial_print("\n");
  return i;
}

/* Legacy Create: File in Root */
int gemfs_create(const char *name) { return gemfs_create_file(-1, name); }

int gemfs_write_id(int id, const char *data, uint32_t size) {
  if (id < 0 || id >= GEMFS_MAX_FILES)
    return -1;

  /* Clamp size */
  if (size > GEMFS_FILE_SECTORS * 512) {
    size = GEMFS_FILE_SECTORS * 512;
  }

  int sectors = (size + 511) / 512;
  int ptr = 0;

  for (int s = 0; s < sectors; s++) {
    gemfs_memset(sector_buf, 0, 512);
    for (int k = 0; k < 512; k++) {
      if ((uint32_t)ptr < size) {
        sector_buf[k] = data[ptr++];
      }
    }
    ata_write_sector(file_table[id].start_lba + s, sector_buf);
  }

  file_table[id].size = size;
  gemfs_sync_table();
  return size;
}

int gemfs_write(const char *name, const char *data, uint32_t size) {
  int idx = gemfs_find(name);
  if (idx < 0) {
    idx = gemfs_create(name);
    if (idx < 0)
      return -1;
  }
  return gemfs_write_id(idx, data, size);
}

int gemfs_read_id(int id, char *buf, uint32_t max_size) {
  if (id < 0 || id >= GEMFS_MAX_FILES)
    return -1;

  uint32_t size = file_table[id].size;
  if (size > max_size - 1)
    size = max_size - 1;

  int sectors = (size + 511) / 512;
  int ptr = 0;

  for (int s = 0; s < sectors; s++) {
    ata_read_sector(file_table[id].start_lba + s, sector_buf);
    for (int k = 0; k < 512; k++) {
      if ((uint32_t)ptr < size) {
        buf[ptr++] = sector_buf[k];
      }
    }
  }
  buf[size] = '\0';
  return size;
}

int gemfs_read(const char *name, char *buf, uint32_t max_size) {
  int idx = gemfs_find(name);
  if (idx < 0)
    return -1;
  return gemfs_read_id(idx, buf, max_size);
}

int gemfs_delete(const char *name) {
  int idx = gemfs_find(name);
  if (idx < 0)
    return -1;

  file_table[idx].name[0] = '\0';
  file_table[idx].size = 0;
  gemfs_sync_table();
  return 0;
}

int gemfs_count(void) {
  int count = 0;
  for (int i = 0; i < GEMFS_MAX_FILES; i++) {
    if (file_table[i].name[0] != '\0')
      count++;
  }
  return count;
}

const char *gemfs_get_name(int index) {
  if (index < 0 || index >= GEMFS_MAX_FILES ||
      file_table[index].name[0] == '\0') {
    return (const char *)0;
  }
  return file_table[index].name;
}

uint32_t gemfs_get_size(int index) {
  if (index >= 0 && index < GEMFS_MAX_FILES)
    return file_table[index].size;
  return 0;
}

uint8_t gemfs_get_type(int index) {
  if (index >= 0 && index < GEMFS_MAX_FILES)
    return file_table[index].type;
  return GEMFS_TYPE_FILE;
}

int8_t gemfs_get_parent(int index) {
  if (index >= 0 && index < GEMFS_MAX_FILES)
    return file_table[index].parent_idx;
  return -1;
}
