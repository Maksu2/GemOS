#ifndef GEMFS_H
#define GEMFS_H

#include <stddef.h>
#include <stdint.h>

/*
 * GemFS v3 (docs/GEMFS.md): files and directories on a whole ATA disk.
 *
 * gemfs_init() mounts the first disk whose superblock is valid. Disks are
 * made by tools/mkgemfs: the kernel never formats, and it never writes to
 * a disk it has not mounted. Without such a disk every call below returns
 * GEMFS_ERR_NOFS and the system runs the programs in the kernel image.
 *
 * Paths are names separated by '/' from the root (a leading '/' is
 * optional), at most GEMFS_PATH_MAX - 1 bytes. Names are 1..GEMFS_NAME_MAX
 * bytes, not "." or "..", case sensitive.
 *
 * Callers are task code (task 0 and syscalls); kernel code is not
 * preempted, so the module needs no lock. Not for interrupt handlers.
 */
#define GEMFS_BLOCK_SIZE 4096U
#define GEMFS_NAME_MAX 55U
#define GEMFS_PATH_MAX 128U
#define GEMFS_MAX_FILE_SIZE (1036U * GEMFS_BLOCK_SIZE) /* 12 direct + 1024 */

#define GEMFS_TYPE_FILE 1U
#define GEMFS_TYPE_DIR 2U

/* A program seeded by the kernel: processes may not change it */
#define GEMFS_FLAG_SYSTEM 0x0001U

enum {
  GEMFS_OK = 0,
  GEMFS_ERR_NOFS = -1,     /* no file system mounted */
  GEMFS_ERR_NOENT = -2,    /* no such file or directory */
  GEMFS_ERR_EXIST = -3,    /* the name is taken */
  GEMFS_ERR_NOTDIR = -4,   /* a path component is not a directory */
  GEMFS_ERR_ISDIR = -5,    /* a directory where a file was expected */
  GEMFS_ERR_NOSPC = -6,    /* no free block, inode or directory slot */
  GEMFS_ERR_TOOBIG = -7,   /* larger than GEMFS_MAX_FILE_SIZE */
  GEMFS_ERR_NAME = -8,     /* bad path or name */
  GEMFS_ERR_IO = -9,       /* the disk failed */
  GEMFS_ERR_CORRUPT = -10, /* metadata that makes no sense */
  GEMFS_ERR_SOURCE = -11,  /* the data source failed (gemfs_write_from) */
  GEMFS_ERR_DENIED = -12,  /* a process may not change it (gemfs_write_user) */
};

typedef struct {
  uint32_t inode;
  uint32_t type;
  uint32_t flags;
  uint32_t size;
  uint32_t version;
} gemfs_stat_t;

typedef struct {
  uint32_t inode;
  uint32_t type;
  char name[GEMFS_NAME_MAX + 1];
} gemfs_entry_t;

typedef struct {
  uint32_t total_blocks;
  uint32_t free_blocks;
  uint32_t total_inodes;
  uint32_t free_inodes;
} gemfs_usage_t;

/* Fills dst with len bytes of the new contents, starting at offset.
 * Returns 0, or non-zero to abandon the write. */
typedef int (*gemfs_source_t)(void *ctx, uint32_t offset, void *dst,
                              uint32_t len);

/* Called for each directory entry. Must not call into GemFS. */
typedef void (*gemfs_list_fn_t)(void *ctx, const gemfs_entry_t *entry);

void gemfs_init(void);
int gemfs_available(void);

/* Changes with every modification, so a view knows when to reread. */
uint32_t gemfs_generation(void);

int gemfs_stat(const char *path, gemfs_stat_t *stat);

/* Up to len bytes of a file from offset; returns the number read. */
int gemfs_read(uint32_t inode, uint32_t offset, void *buf, uint32_t len);

/* Create or replace a file. The old contents stay until the new ones are
 * on the disk. Returns the size written. */
int gemfs_write(const char *path, const void *data, uint32_t size,
                uint32_t flags, uint32_t version);
int gemfs_write_from(const char *path, uint32_t size, gemfs_source_t source,
                     void *ctx, uint32_t flags, uint32_t version);

/* gemfs_write_from for a process: a program (a name ending in ".ELF", in
 * any case) or a system file is never created or replaced. */
int gemfs_write_user(const char *path, uint32_t size, gemfs_source_t source,
                     void *ctx);

int gemfs_mkdir(const char *path);

/* Delete a file, or a directory with everything in it. */
int gemfs_delete(const char *path);

/* Call fn for every entry of a directory; returns the number of entries. */
int gemfs_list(const char *path, gemfs_list_fn_t fn, void *ctx);

int gemfs_usage(gemfs_usage_t *usage);

/* Short English description of a GEMFS_ERR_* code, for logs. */
const char *gemfs_error(int error);

#endif /* GEMFS_H */
