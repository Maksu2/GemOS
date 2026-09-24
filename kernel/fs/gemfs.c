/*
 * GemFS v3, the format is in docs/GEMFS.md.
 *
 * The whole bitmap and a bitmap of used inodes stay in memory; inodes,
 * directories and data are read from the disk when needed. Writes follow
 * the update order of docs/GEMFS.md: a crash leaves at worst blocks or
 * inodes marked used that nothing points to (tools/mkgemfs check lists
 * them), never a pointer to a free block.
 *
 * The scratch buffers below are shared by all calls: a function that
 * fills one never calls another function that uses the same buffer.
 */
#include "gemfs.h"

#include "../../drivers/ata.h"
#include "../../drivers/serial.h"
#include "../include/heap.h"
#include "crc32.h"
#include <string.h>

#define SECTOR_SIZE 512U
#define SECTORS_PER_BLOCK (GEMFS_BLOCK_SIZE / SECTOR_SIZE)
#define BITS_PER_BLOCK (GEMFS_BLOCK_SIZE * 8U)
#define INODE_SIZE 128U
#define INODES_PER_BLOCK (GEMFS_BLOCK_SIZE / INODE_SIZE)
#define INODES_PER_SECTOR (SECTOR_SIZE / INODE_SIZE)
#define ENTRY_SIZE 64U
#define ENTRIES_PER_BLOCK (GEMFS_BLOCK_SIZE / ENTRY_SIZE)
#define DIRECT 12U
#define POINTERS (GEMFS_BLOCK_SIZE / 4U)
#define MAX_FILE_BLOCKS (DIRECT + POINTERS)
#define MAX_FS_BLOCKS (1U << 20)
#define MIN_INODES 64U
#define MAX_INODES 8192U
#define ROOT 1U
#define TYPE_FREE 0U
#define MAX_DEPTH (GEMFS_PATH_MAX / 2U) /* "a/b/c...": 2 bytes a level */

static const char gemfs_magic[8] = {'G', 'E', 'M', 'O', 'S', '-', 'F', 'S'};

typedef struct {
  char magic[8];
  uint32_t version;
  uint32_t block_size;
  uint32_t total_blocks;
  uint32_t bitmap_start;
  uint32_t bitmap_blocks;
  uint32_t inode_start;
  uint32_t inode_blocks;
  uint32_t inode_count;
  uint32_t data_start;
  uint32_t root_inode;
  char label[16];
  uint8_t reserved[444];
  uint32_t checksum;
} __attribute__((packed)) gemfs_super_t;

typedef struct {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
  uint32_t parent;
  uint32_t version;
  uint32_t direct[DIRECT];
  uint32_t indirect;
  uint32_t reserved[15];
} __attribute__((packed)) gemfs_inode_t;

typedef struct {
  uint32_t inode;
  uint8_t type;
  uint8_t reserved[3];
  char name[56];
} __attribute__((packed)) gemfs_dirent_t;

_Static_assert(sizeof(gemfs_super_t) == SECTOR_SIZE, "superblock size");
_Static_assert(sizeof(gemfs_inode_t) == INODE_SIZE, "inode size");
_Static_assert(sizeof(gemfs_dirent_t) == ENTRY_SIZE, "entry size");

static int fs_device = -1; /* mounted ATA device, -1 = none */
static gemfs_super_t sb;
static uint8_t *bitmap;     /* sb.bitmap_blocks blocks */
static uint8_t *inode_used; /* one bit per inode */
static uint32_t free_blocks;
static uint32_t free_inodes;
static uint32_t alloc_hint;
static uint32_t generation;
static uint32_t dirty_lo = 0xFFFFFFFFU; /* bitmap sectors to write */
static uint32_t dirty_hi;

static uint8_t data_buf[GEMFS_BLOCK_SIZE];  /* file data */
static uint8_t dir_buf[GEMFS_BLOCK_SIZE];   /* directory blocks */
static uint32_t map_buf[POINTERS];          /* an indirect block */
static uint32_t map_block;                  /* block in map_buf, 0 = none */
static uint8_t sector_buf[SECTOR_SIZE];     /* superblock, inode sectors */

/* -- disk ------------------------------------------------------------------ */

static int dev_read(uint32_t lba, uint32_t sectors, void *buf) {
  return ata_read(fs_device, lba, sectors, buf) == ATA_OK ? GEMFS_OK
                                                           : GEMFS_ERR_IO;
}

static int dev_write(uint32_t lba, uint32_t sectors, const void *buf) {
  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  return ata_write(fs_device, lba, sectors, buf) == ATA_OK ? GEMFS_OK
                                                            : GEMFS_ERR_IO;
}

static int block_read(uint32_t block, void *buf) {
  return dev_read(block * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf);
}

static int data_block_ok(uint32_t block) {
  return block >= sb.data_start && block < sb.total_blocks;
}

/* Blocks of files and directories only: a bad block number can never
 * overwrite the superblock, the bitmap or the inode table. */
static int block_write(uint32_t block, const void *buf) {
  if (!data_block_ok(block)) {
    return GEMFS_ERR_CORRUPT;
  }
  return dev_write(block * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK, buf);
}

/* -- block bitmap ------------------------------------------------------------ */

static int block_in_use(uint32_t block) {
  return (bitmap[block / 8U] >> (block % 8U)) & 1U;
}

static void block_mark(uint32_t block, int used) {
  uint32_t sector = block / 8U / SECTOR_SIZE;

  if (used) {
    bitmap[block / 8U] |= (uint8_t)(1U << (block % 8U));
  } else {
    bitmap[block / 8U] &= (uint8_t)~(1U << (block % 8U));
  }
  if (dirty_lo == 0xFFFFFFFFU) {
    dirty_lo = sector;
    dirty_hi = sector;
  } else if (sector < dirty_lo) {
    dirty_lo = sector;
  } else if (sector > dirty_hi) {
    dirty_hi = sector;
  }
}

/* Write the changed bitmap sectors. */
static int bitmap_sync(void) {
  int result = GEMFS_OK;

  if (dirty_lo == 0xFFFFFFFFU) {
    return GEMFS_OK;
  }
  for (uint32_t s = dirty_lo; s <= dirty_hi && result == GEMFS_OK; ++s) {
    result = dev_write(sb.bitmap_start * SECTORS_PER_BLOCK + s, 1,
                       bitmap + s * SECTOR_SIZE);
  }
  dirty_lo = 0xFFFFFFFFU;
  dirty_hi = 0;
  return result;
}

static uint32_t block_alloc(void) {
  uint32_t span = sb.total_blocks - sb.data_start;

  for (uint32_t i = 0; i < span; ++i) {
    uint32_t block = sb.data_start + (alloc_hint + i) % span;

    if (!block_in_use(block)) {
      block_mark(block, 1);
      free_blocks--;
      alloc_hint = block + 1U - sb.data_start;
      return block;
    }
  }
  return 0;
}

static void block_release(uint32_t block) {
  if (data_block_ok(block) && block_in_use(block)) {
    block_mark(block, 0);
    free_blocks++;
  }
}

/* -- inodes ---------------------------------------------------------------- */

static int inode_ok(uint32_t number) {
  return number >= ROOT && number < sb.inode_count;
}

static uint32_t inode_lba(uint32_t number) {
  return (sb.inode_start + number / INODES_PER_BLOCK) * SECTORS_PER_BLOCK +
         (number % INODES_PER_BLOCK) / INODES_PER_SECTOR;
}

static int inode_read(uint32_t number, gemfs_inode_t *inode) {
  int result;

  if (!inode_ok(number)) {
    return GEMFS_ERR_CORRUPT;
  }
  result = dev_read(inode_lba(number), 1, sector_buf);
  if (result == GEMFS_OK) {
    memcpy(inode, sector_buf + (number % INODES_PER_SECTOR) * INODE_SIZE,
           INODE_SIZE);
  }
  return result;
}

static int inode_write(uint32_t number, const gemfs_inode_t *inode) {
  int result;

  if (!inode_ok(number)) {
    return GEMFS_ERR_CORRUPT;
  }
  result = dev_read(inode_lba(number), 1, sector_buf);
  if (result == GEMFS_OK) {
    memcpy(sector_buf + (number % INODES_PER_SECTOR) * INODE_SIZE, inode,
           INODE_SIZE);
    result = dev_write(inode_lba(number), 1, sector_buf);
  }
  return result;
}

static uint32_t inode_alloc(void) {
  for (uint32_t number = ROOT + 1U; number < sb.inode_count; ++number) {
    if (!(inode_used[number / 8U] & (1U << (number % 8U)))) {
      inode_used[number / 8U] |= (uint8_t)(1U << (number % 8U));
      free_inodes--;
      return number;
    }
  }
  return 0;
}

static void inode_release(uint32_t number) {
  if (inode_ok(number) && number != ROOT &&
      (inode_used[number / 8U] & (1U << (number % 8U)))) {
    inode_used[number / 8U] &= (uint8_t)~(1U << (number % 8U));
    free_inodes++;
  }
}

static uint32_t inode_block_count(const gemfs_inode_t *inode) {
  return (inode->size + GEMFS_BLOCK_SIZE - 1U) / GEMFS_BLOCK_SIZE;
}

/* Disk block of block `index` of an inode. */
static int inode_map(const gemfs_inode_t *inode, uint32_t index,
                     uint32_t *block) {
  uint32_t found;

  if (index >= inode_block_count(inode) || index >= MAX_FILE_BLOCKS) {
    return GEMFS_ERR_CORRUPT;
  }
  if (index < DIRECT) {
    found = inode->direct[index];
  } else {
    if (!data_block_ok(inode->indirect)) {
      return GEMFS_ERR_CORRUPT;
    }
    if (map_block != inode->indirect) {
      int result = block_read(inode->indirect, map_buf);

      if (result != GEMFS_OK) {
        map_block = 0;
        return result;
      }
      map_block = inode->indirect;
    }
    found = map_buf[index - DIRECT];
  }
  if (!data_block_ok(found)) {
    return GEMFS_ERR_CORRUPT;
  }
  *block = found;
  return GEMFS_OK;
}

/* Mark every block of an inode free (in memory; bitmap_sync writes). */
static void inode_release_blocks(const gemfs_inode_t *inode) {
  uint32_t count = inode_block_count(inode);

  for (uint32_t i = 0; i < count && i < MAX_FILE_BLOCKS; ++i) {
    uint32_t block;

    if (inode_map(inode, i, &block) == GEMFS_OK) {
      block_release(block);
    }
  }
  if (count > DIRECT) {
    block_release(inode->indirect);
  }
}

/* -- paths ------------------------------------------------------------------- */

/* Check a whole path; *rest points past the leading '/'. */
static int path_check(const char *path, const char **rest) {
  size_t length;

  if (path == NULL) {
    return GEMFS_ERR_NAME;
  }
  length = strlen(path);
  if (length >= GEMFS_PATH_MAX) {
    return GEMFS_ERR_NAME;
  }
  *rest = path[0] == '/' ? path + 1 : path;
  return GEMFS_OK;
}

/* Copy the next name of *path into name and move past it. Returns 1 for a
 * name, 0 at the end, GEMFS_ERR_NAME for an empty or bad name. */
static int path_next(const char **path, char name[GEMFS_NAME_MAX + 1]) {
  const char *p = *path;
  size_t length = 0;

  if (*p == '\0') {
    return 0;
  }
  while (p[length] != '\0' && p[length] != '/') {
    length++;
  }
  if (length == 0 || length > GEMFS_NAME_MAX) {
    return GEMFS_ERR_NAME;
  }
  memcpy(name, p, length);
  name[length] = '\0';
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    return GEMFS_ERR_NAME;
  }
  p += length;
  if (*p == '/') {
    p++; /* one trailing '/' is allowed */
  }
  *path = p;
  return 1;
}

/* Look name up in a directory; *number = 0 if it is not there. */
static int dir_find(const gemfs_inode_t *dir, const char *name,
                    uint32_t *number, uint32_t *type) {
  uint32_t blocks = inode_block_count(dir);

  *number = 0;
  for (uint32_t i = 0; i < blocks; ++i) {
    uint32_t block;
    int result = inode_map(dir, i, &block);

    if (result == GEMFS_OK) {
      result = block_read(block, dir_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    for (uint32_t slot = 0; slot < ENTRIES_PER_BLOCK; ++slot) {
      const gemfs_dirent_t *entry =
          (const gemfs_dirent_t *)(dir_buf + slot * ENTRY_SIZE);

      if (entry->inode != 0 &&
          strncmp(entry->name, name, sizeof(entry->name)) == 0) {
        if (!inode_ok(entry->inode) || entry->inode == ROOT) {
          return GEMFS_ERR_CORRUPT;
        }
        *number = entry->inode;
        *type = entry->type;
        return GEMFS_OK;
      }
    }
  }
  return GEMFS_OK;
}

/* Walk a path. With want_parent, stop before the last name: *number is the
 * directory that should hold it and last receives the name. */
static int path_walk(const char *path, int want_parent, uint32_t *number,
                     char last[GEMFS_NAME_MAX + 1]) {
  char name[GEMFS_NAME_MAX + 1];
  const char *rest;
  uint32_t current = ROOT;
  int result = path_check(path, &rest);

  if (result != GEMFS_OK) {
    return result;
  }
  for (;;) {
    gemfs_inode_t dir;
    uint32_t next;
    uint32_t type;

    result = path_next(&rest, name);
    if (result < 0) {
      return result;
    }
    if (result == 0) {
      if (want_parent) {
        return GEMFS_ERR_NAME; /* the root has no name to create */
      }
      *number = current;
      return GEMFS_OK;
    }
    result = inode_read(current, &dir);
    if (result != GEMFS_OK) {
      return result;
    }
    if (dir.type != GEMFS_TYPE_DIR) {
      return GEMFS_ERR_NOTDIR;
    }
    if (want_parent && *rest == '\0') {
      memcpy(last, name, sizeof(name));
      *number = current;
      return GEMFS_OK;
    }
    result = dir_find(&dir, name, &next, &type);
    if (result != GEMFS_OK) {
      return result;
    }
    if (next == 0) {
      return GEMFS_ERR_NOENT;
    }
    current = next;
  }
}

/* -- directories -------------------------------------------------------------- */

/* Add an entry to directory dir_number. */
static int dir_add(uint32_t dir_number, const char *name, uint32_t number,
                   uint32_t type) {
  gemfs_inode_t dir;
  uint32_t blocks;
  uint32_t block = 0;
  int result = inode_read(dir_number, &dir);

  if (result != GEMFS_OK) {
    return result;
  }
  blocks = inode_block_count(&dir);
  for (uint32_t i = 0; i < blocks; ++i) {
    result = inode_map(&dir, i, &block);
    if (result == GEMFS_OK) {
      result = block_read(block, dir_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    for (uint32_t slot = 0; slot < ENTRIES_PER_BLOCK; ++slot) {
      gemfs_dirent_t *entry = (gemfs_dirent_t *)(dir_buf + slot * ENTRY_SIZE);

      if (entry->inode == 0) {
        memset(entry, 0, sizeof(*entry));
        entry->inode = number;
        entry->type = (uint8_t)type;
        strncpy(entry->name, name, sizeof(entry->name));
        return block_write(block, dir_buf);
      }
    }
  }

  /* all blocks full: one more block (directories use direct blocks only) */
  if (blocks >= DIRECT) {
    return GEMFS_ERR_NOSPC;
  }
  block = block_alloc();
  if (block == 0) {
    return GEMFS_ERR_NOSPC;
  }
  result = bitmap_sync();
  if (result == GEMFS_OK) {
    gemfs_dirent_t *entry = (gemfs_dirent_t *)dir_buf;

    memset(dir_buf, 0, sizeof(dir_buf));
    entry->inode = number;
    entry->type = (uint8_t)type;
    strncpy(entry->name, name, sizeof(entry->name));
    result = block_write(block, dir_buf);
  }
  if (result == GEMFS_OK) {
    dir.direct[blocks] = block;
    dir.size += GEMFS_BLOCK_SIZE;
    result = inode_write(dir_number, &dir);
  }
  if (result != GEMFS_OK) {
    block_release(block);
    (void)bitmap_sync();
  }
  return result;
}

/* Remove the entry for inode number from directory dir_number. */
static int dir_remove(uint32_t dir_number, uint32_t number) {
  gemfs_inode_t dir;
  uint32_t blocks;
  int result = inode_read(dir_number, &dir);

  if (result != GEMFS_OK) {
    return result;
  }
  blocks = inode_block_count(&dir);
  for (uint32_t i = 0; i < blocks; ++i) {
    uint32_t block;

    result = inode_map(&dir, i, &block);
    if (result == GEMFS_OK) {
      result = block_read(block, dir_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    for (uint32_t slot = 0; slot < ENTRIES_PER_BLOCK; ++slot) {
      gemfs_dirent_t *entry = (gemfs_dirent_t *)(dir_buf + slot * ENTRY_SIZE);

      if (entry->inode == number) {
        memset(entry, 0, sizeof(*entry));
        return block_write(block, dir_buf);
      }
    }
  }
  return GEMFS_ERR_CORRUPT;
}

/* First entry of a directory, *number = 0 if it is empty. */
static int dir_first(uint32_t dir_number, uint32_t *number) {
  gemfs_inode_t dir;
  uint32_t blocks;
  int result = inode_read(dir_number, &dir);

  *number = 0;
  if (result != GEMFS_OK) {
    return result;
  }
  blocks = inode_block_count(&dir);
  for (uint32_t i = 0; i < blocks; ++i) {
    uint32_t block;

    result = inode_map(&dir, i, &block);
    if (result == GEMFS_OK) {
      result = block_read(block, dir_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    for (uint32_t slot = 0; slot < ENTRIES_PER_BLOCK; ++slot) {
      const gemfs_dirent_t *entry =
          (const gemfs_dirent_t *)(dir_buf + slot * ENTRY_SIZE);

      if (entry->inode != 0) {
        if (!inode_ok(entry->inode) || entry->inode == ROOT) {
          return GEMFS_ERR_CORRUPT;
        }
        *number = entry->inode;
        return GEMFS_OK;
      }
    }
  }
  return GEMFS_OK;
}

/* -- mounting ------------------------------------------------------------------ */

static const char *gemfs_check_super(const gemfs_super_t *s,
                                     uint32_t disk_sectors) {
  uint32_t bitmap_blocks;
  uint32_t inode_blocks;

  if (memcmp(s->magic, gemfs_magic, sizeof(gemfs_magic)) != 0) {
    return "no GemFS signature";
  }
  if (s->version != 3U || s->block_size != GEMFS_BLOCK_SIZE) {
    return "unsupported GemFS version";
  }
  if (crc32(s, offsetof(gemfs_super_t, checksum)) != s->checksum) {
    return "superblock checksum mismatch";
  }
  if (s->inode_count < MIN_INODES || s->inode_count > MAX_INODES ||
      s->inode_count % INODES_PER_BLOCK != 0 || s->root_inode != ROOT) {
    return "bad inode table";
  }
  if (s->total_blocks > MAX_FS_BLOCKS ||
      s->total_blocks > disk_sectors / SECTORS_PER_BLOCK) {
    return "larger than the disk";
  }
  bitmap_blocks = (s->total_blocks + BITS_PER_BLOCK - 1U) / BITS_PER_BLOCK;
  inode_blocks = s->inode_count / INODES_PER_BLOCK;
  if (s->bitmap_start != 1U || s->bitmap_blocks != bitmap_blocks ||
      s->inode_start != 1U + bitmap_blocks || s->inode_blocks != inode_blocks ||
      s->data_start != s->inode_start + inode_blocks ||
      s->data_start >= s->total_blocks) {
    return "inconsistent layout";
  }
  return NULL;
}

static void gemfs_unmount_memory(void) {
  if (bitmap != NULL) {
    kfree(bitmap);
    bitmap = NULL;
  }
  if (inode_used != NULL) {
    kfree(inode_used);
    inode_used = NULL;
  }
}

/* Read the bitmap and scan the inode table; nothing is written. */
static const char *gemfs_load(int device) {
  gemfs_inode_t root;
  uint32_t bitmap_bytes = sb.bitmap_blocks * GEMFS_BLOCK_SIZE;

  bitmap = (uint8_t *)kalloc(bitmap_bytes);
  inode_used = (uint8_t *)kalloc(sb.inode_count / 8U);
  if (bitmap == NULL || inode_used == NULL) {
    return "no memory for the bitmap";
  }
  fs_device = device; /* for dev_read; the disk is still read-only */
  for (uint32_t i = 0; i < sb.bitmap_blocks; ++i) {
    if (block_read(sb.bitmap_start + i, bitmap + i * GEMFS_BLOCK_SIZE) !=
        GEMFS_OK) {
      return "cannot read the bitmap";
    }
  }
  for (uint32_t block = 0; block < sb.data_start; ++block) {
    if (!block_in_use(block)) {
      return "bitmap marks metadata free";
    }
  }
  free_blocks = 0;
  for (uint32_t block = sb.data_start; block < sb.total_blocks; ++block) {
    if (!block_in_use(block)) {
      free_blocks++;
    }
  }

  memset(inode_used, 0, sb.inode_count / 8U);
  free_inodes = 0;
  for (uint32_t i = 0; i < sb.inode_blocks; ++i) {
    if (block_read(sb.inode_start + i, data_buf) != GEMFS_OK) {
      return "cannot read the inode table";
    }
    for (uint32_t j = 0; j < INODES_PER_BLOCK; ++j) {
      uint32_t number = i * INODES_PER_BLOCK + j;
      const gemfs_inode_t *inode =
          (const gemfs_inode_t *)(data_buf + j * INODE_SIZE);

      if (number == 0) {
        continue; /* never used */
      }
      if (inode->type > GEMFS_TYPE_DIR) {
        return "bad inode type";
      }
      if (inode->type != TYPE_FREE) {
        inode_used[number / 8U] |= (uint8_t)(1U << (number % 8U));
      } else {
        free_inodes++;
      }
    }
  }
  if (inode_read(ROOT, &root) != GEMFS_OK || root.type != GEMFS_TYPE_DIR) {
    return "no root directory";
  }
  return NULL;
}

void gemfs_init(void) {
  ata_init();

  for (int device = 0; device < ATA_MAX_DEVICES && fs_device < 0; ++device) {
    const ata_device_t *dev = ata_get_device(device);
    const char *problem;

    if (dev == NULL) {
      continue;
    }
    if (ata_read(device, 0, 1, sector_buf) != ATA_OK) {
      problem = "cannot read the superblock";
    } else {
      memcpy(&sb, sector_buf, sizeof(sb));
      problem = gemfs_check_super(&sb, dev->sectors);
      if (problem == NULL) {
        problem = gemfs_load(device);
      }
    }
    if (problem != NULL) {
      fs_device = -1;
      gemfs_unmount_memory();
      serial_print("[GemFS] ");
      serial_print(dev->model);
      serial_print(": ");
      serial_print(problem);
      serial_print(", not mounted\n");
      continue;
    }

    alloc_hint = 0;
    map_block = 0;
    ata_allow_writes(device); /* the only disk the kernel may change */
    serial_print("[GemFS] Mounted GemFS v3 on ");
    serial_print(dev->model);
    serial_print(": ");
    serial_print_dec(sb.total_blocks * 4U);
    serial_print(" KB, ");
    serial_print_dec(free_blocks * 4U);
    serial_print(" KB and ");
    serial_print_dec(free_inodes);
    serial_print(" inodes free\n");
  }

  if (fs_device < 0) {
    serial_print("[GemFS] No GemFS disk: running without a file system\n");
  }
}

int gemfs_available(void) { return fs_device >= 0; }

uint32_t gemfs_generation(void) { return generation; }

/* -- files ------------------------------------------------------------------- */

int gemfs_stat(const char *path, gemfs_stat_t *stat) {
  gemfs_inode_t inode;
  uint32_t number;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  map_block = 0;
  result = path_walk(path, 0, &number, NULL);
  if (result == GEMFS_OK) {
    result = inode_read(number, &inode);
  }
  if (result != GEMFS_OK) {
    return result;
  }
  if (inode.type != GEMFS_TYPE_FILE && inode.type != GEMFS_TYPE_DIR) {
    return GEMFS_ERR_CORRUPT;
  }
  stat->inode = number;
  stat->type = inode.type;
  stat->flags = inode.flags;
  stat->size = inode.size;
  stat->version = inode.version;
  return GEMFS_OK;
}

int gemfs_read(uint32_t number, uint32_t offset, void *buf, uint32_t len) {
  gemfs_inode_t inode;
  uint8_t *out = (uint8_t *)buf;
  uint32_t done = 0;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  map_block = 0;
  result = inode_read(number, &inode);
  if (result != GEMFS_OK) {
    return result;
  }
  if (inode.type != GEMFS_TYPE_FILE) {
    return inode.type == GEMFS_TYPE_DIR ? GEMFS_ERR_ISDIR : GEMFS_ERR_NOENT;
  }
  if (inode.size > GEMFS_MAX_FILE_SIZE) {
    return GEMFS_ERR_CORRUPT;
  }
  if (offset >= inode.size) {
    return 0;
  }
  if (len > inode.size - offset) {
    len = inode.size - offset;
  }
  while (done < len) {
    uint32_t position = offset + done;
    uint32_t within = position % GEMFS_BLOCK_SIZE;
    uint32_t chunk = GEMFS_BLOCK_SIZE - within;
    uint32_t block;

    if (chunk > len - done) {
      chunk = len - done;
    }
    result = inode_map(&inode, position / GEMFS_BLOCK_SIZE, &block);
    if (result == GEMFS_OK) {
      result = block_read(block, data_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    memcpy(out + done, data_buf + within, chunk);
    done += chunk;
  }
  return (int)len;
}

static int gemfs_memory_source(void *ctx, uint32_t offset, void *dst,
                               uint32_t len) {
  memcpy(dst, (const uint8_t *)ctx + offset, len);
  return 0;
}

int gemfs_write(const char *path, const void *data, uint32_t size,
                uint32_t flags, uint32_t version) {
  return gemfs_write_from(path, size, gemfs_memory_source, (void *)data, flags,
                          version);
}

/* Give back the blocks of a write that did not complete. */
static void gemfs_undo_blocks(const gemfs_inode_t *inode, uint32_t allocated) {
  for (uint32_t i = 0; i < allocated; ++i) {
    block_release(i < DIRECT ? inode->direct[i] : map_buf[i - DIRECT]);
  }
  if (inode->indirect != 0) {
    block_release(inode->indirect);
  }
  map_block = 0;
  (void)bitmap_sync();
}

int gemfs_write_from(const char *path, uint32_t size, gemfs_source_t source,
                     void *ctx, uint32_t flags, uint32_t version) {
  char name[GEMFS_NAME_MAX + 1];
  gemfs_inode_t fresh;
  gemfs_inode_t old;
  uint32_t parent;
  uint32_t existing;
  uint32_t type = 0;
  uint32_t count;
  uint32_t number;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  if (size > GEMFS_MAX_FILE_SIZE) {
    return GEMFS_ERR_TOOBIG;
  }
  map_block = 0;
  result = path_walk(path, 1, &parent, name);
  if (result == GEMFS_OK) {
    result = inode_read(parent, &old);
  }
  if (result == GEMFS_OK) {
    result = dir_find(&old, name, &existing, &type);
  }
  if (result != GEMFS_OK) {
    return result;
  }
  if (existing != 0 && type != GEMFS_TYPE_FILE) {
    return GEMFS_ERR_ISDIR;
  }
  count = (size + GEMFS_BLOCK_SIZE - 1U) / GEMFS_BLOCK_SIZE;
  /* data, the indirect block, and maybe a block for the directory entry */
  if (count + (count > DIRECT) + (existing == 0) > free_blocks ||
      (existing == 0 && free_inodes == 0)) {
    return GEMFS_ERR_NOSPC;
  }

  /* 1. new blocks, marked used on the disk before anything points to them */
  memset(&fresh, 0, sizeof(fresh));
  fresh.type = GEMFS_TYPE_FILE;
  fresh.flags = (uint16_t)flags;
  fresh.size = size;
  fresh.parent = parent;
  fresh.version = version;
  map_block = 0; /* map_buf holds the new table, not a cached block */
  if (count > DIRECT) {
    memset(map_buf, 0, sizeof(map_buf));
    fresh.indirect = block_alloc();
    if (fresh.indirect == 0) {
      return GEMFS_ERR_NOSPC;
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t block = block_alloc();

    if (block == 0) { /* the free count was wrong */
      gemfs_undo_blocks(&fresh, i);
      return GEMFS_ERR_CORRUPT;
    }
    if (i < DIRECT) {
      fresh.direct[i] = block;
    } else {
      map_buf[i - DIRECT] = block;
    }
  }
  result = bitmap_sync();

  /* 2. the data */
  for (uint32_t i = 0; i < count && result == GEMFS_OK; ++i) {
    uint32_t offset = i * GEMFS_BLOCK_SIZE;
    uint32_t chunk = size - offset < GEMFS_BLOCK_SIZE ? size - offset
                                                       : GEMFS_BLOCK_SIZE;

    memset(data_buf + chunk, 0, GEMFS_BLOCK_SIZE - chunk);
    if (source(ctx, offset, data_buf, chunk) != 0) {
      result = GEMFS_ERR_SOURCE;
      break;
    }
    result = block_write(i < DIRECT ? fresh.direct[i] : map_buf[i - DIRECT],
                         data_buf);
  }
  if (result == GEMFS_OK && count > DIRECT) {
    result = block_write(fresh.indirect, map_buf);
  }
  if (result != GEMFS_OK) {
    gemfs_undo_blocks(&fresh, count);
    return result;
  }

  /* 3. the inode: for a new file before its directory entry */
  if (existing == 0) {
    number = inode_alloc();
    result = inode_write(number, &fresh);
    if (result == GEMFS_OK) {
      result = dir_add(parent, name, number, GEMFS_TYPE_FILE);
    }
    if (result != GEMFS_OK) {
      memset(&old, 0, sizeof(old));
      (void)inode_write(number, &old);
      inode_release(number);
      gemfs_undo_blocks(&fresh, count);
      return result;
    }
  } else {
    result = inode_read(existing, &old);
    if (result == GEMFS_OK) {
      result = inode_write(existing, &fresh); /* the commit */
    }
    if (result != GEMFS_OK) {
      gemfs_undo_blocks(&fresh, count);
      return result;
    }
    /* 4. the old contents */
    map_block = 0;
    inode_release_blocks(&old);
    result = bitmap_sync();
  }
  generation++;
  return result == GEMFS_OK ? (int)size : result;
}

int gemfs_mkdir(const char *path) {
  char name[GEMFS_NAME_MAX + 1];
  gemfs_inode_t dir;
  uint32_t parent;
  uint32_t existing;
  uint32_t type;
  uint32_t number;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  map_block = 0;
  result = path_walk(path, 1, &parent, name);
  if (result == GEMFS_OK) {
    result = inode_read(parent, &dir);
  }
  if (result == GEMFS_OK) {
    result = dir_find(&dir, name, &existing, &type);
  }
  if (result != GEMFS_OK) {
    return result;
  }
  if (existing != 0) {
    return GEMFS_ERR_EXIST;
  }
  if (free_inodes == 0) {
    return GEMFS_ERR_NOSPC;
  }
  number = inode_alloc();
  memset(&dir, 0, sizeof(dir));
  dir.type = GEMFS_TYPE_DIR;
  dir.parent = parent;
  result = inode_write(number, &dir);
  if (result == GEMFS_OK) {
    result = dir_add(parent, name, number, GEMFS_TYPE_DIR);
  }
  if (result != GEMFS_OK) {
    memset(&dir, 0, sizeof(dir));
    (void)inode_write(number, &dir);
    inode_release(number);
    return result;
  }
  generation++;
  return GEMFS_OK;
}

/* Unlink one inode that is a file or an empty directory, then free it. */
static int gemfs_remove_one(uint32_t number) {
  gemfs_inode_t inode;
  gemfs_inode_t empty;
  int result = inode_read(number, &inode);

  if (result == GEMFS_OK) {
    result = dir_remove(inode.parent, number);
  }
  if (result != GEMFS_OK) {
    return result;
  }
  map_block = 0;
  inode_release_blocks(&inode);
  result = bitmap_sync();
  memset(&empty, 0, sizeof(empty));
  if (result == GEMFS_OK) {
    result = inode_write(number, &empty);
  }
  if (result == GEMFS_OK) {
    inode_release(number);
  }
  return result;
}

int gemfs_delete(const char *path) {
  uint32_t stack[MAX_DEPTH];
  uint32_t depth = 0;
  uint32_t target;
  uint32_t current;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  map_block = 0;
  result = path_walk(path, 0, &target, NULL);
  if (result != GEMFS_OK) {
    return result;
  }
  if (target == ROOT) {
    return GEMFS_ERR_NAME;
  }

  /* depth first: a directory goes after everything in it */
  current = target;
  for (;;) {
    gemfs_inode_t inode;
    uint32_t child = 0;

    result = inode_read(current, &inode);
    if (result == GEMFS_OK && inode.type == GEMFS_TYPE_DIR) {
      result = dir_first(current, &child);
    }
    if (result != GEMFS_OK) {
      break;
    }
    if (child != 0) {
      if (depth == MAX_DEPTH) {
        result = GEMFS_ERR_CORRUPT;
        break;
      }
      stack[depth++] = current;
      current = child;
      continue;
    }
    result = gemfs_remove_one(current);
    if (result != GEMFS_OK || current == target) {
      break;
    }
    current = stack[--depth];
  }
  generation++;
  return result;
}

int gemfs_list(const char *path, gemfs_list_fn_t fn, void *ctx) {
  gemfs_inode_t dir;
  uint32_t number;
  uint32_t blocks;
  int count = 0;
  int result;

  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  map_block = 0;
  result = path_walk(path, 0, &number, NULL);
  if (result == GEMFS_OK) {
    result = inode_read(number, &dir);
  }
  if (result != GEMFS_OK) {
    return result;
  }
  if (dir.type != GEMFS_TYPE_DIR) {
    return GEMFS_ERR_NOTDIR;
  }
  blocks = inode_block_count(&dir);
  for (uint32_t i = 0; i < blocks; ++i) {
    uint32_t block;

    result = inode_map(&dir, i, &block);
    if (result == GEMFS_OK) {
      result = block_read(block, dir_buf);
    }
    if (result != GEMFS_OK) {
      return result;
    }
    for (uint32_t slot = 0; slot < ENTRIES_PER_BLOCK; ++slot) {
      const gemfs_dirent_t *entry =
          (const gemfs_dirent_t *)(dir_buf + slot * ENTRY_SIZE);
      gemfs_entry_t info;

      if (entry->inode == 0) {
        continue;
      }
      info.inode = entry->inode;
      info.type = entry->type;
      memcpy(info.name, entry->name, GEMFS_NAME_MAX);
      info.name[GEMFS_NAME_MAX] = '\0';
      if (fn != NULL) {
        fn(ctx, &info);
      }
      count++;
    }
  }
  return count;
}

int gemfs_usage(gemfs_usage_t *usage) {
  if (fs_device < 0) {
    return GEMFS_ERR_NOFS;
  }
  usage->total_blocks = sb.total_blocks;
  usage->free_blocks = free_blocks;
  usage->total_inodes = sb.inode_count - 1U;
  usage->free_inodes = free_inodes;
  return GEMFS_OK;
}

const char *gemfs_error(int error) {
  switch (error) {
  case GEMFS_OK: return "ok";
  case GEMFS_ERR_NOFS: return "no file system";
  case GEMFS_ERR_NOENT: return "no such file or directory";
  case GEMFS_ERR_EXIST: return "already exists";
  case GEMFS_ERR_NOTDIR: return "not a directory";
  case GEMFS_ERR_ISDIR: return "is a directory";
  case GEMFS_ERR_NOSPC: return "disk full";
  case GEMFS_ERR_TOOBIG: return "file too big";
  case GEMFS_ERR_NAME: return "bad name";
  case GEMFS_ERR_IO: return "disk error";
  case GEMFS_ERR_CORRUPT: return "corrupted file system";
  case GEMFS_ERR_SOURCE: return "cannot read the data";
  default: return "unknown error";
  }
}
