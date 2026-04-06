/**
 * GemFS v0 - Minimal RAM-based File System
 *
 * Simple file storage for Text Editor.
 * No directories, no permissions, fixed slots.
 */

#ifndef GEMFS_H
#define GEMFS_H

#include <stdint.h>

#define GEMFS_MAX_FILES 64
#define GEMFS_MAX_FILENAME 20 /* Reduced to fit metadata */
#define GEMFS_MAX_FILESIZE 8192

#define GEMFS_TYPE_FILE 0
#define GEMFS_TYPE_DIR 1

typedef struct {
  char name[GEMFS_MAX_FILENAME];
  uint32_t size;
  char data[GEMFS_MAX_FILESIZE];
  int used;
  uint8_t type;
  int8_t parent_id; /* -1 for root */
} gemfs_file_t;

/* Initialize file system */
void gemfs_init(void);

/* Create file/dir (returns index) */
int gemfs_create_file(int parent_id, const char *name);
int gemfs_create_dir(int parent_id, const char *name);

/* Legacy create (root file) */
int gemfs_create(const char *name);

/* Write data to file (returns bytes written or -1) */
int gemfs_write(const char *name, const char *data, uint32_t size);

/* Write by ID (safer) */
int gemfs_write_id(int id, const char *data, uint32_t size);

/* Read data from file (returns bytes read or -1) */
int gemfs_read(const char *name, char *buf, uint32_t max_size);
int gemfs_read_id(int id, char *buf, uint32_t max_size);

/* Delete a file (returns 0 on success, -1 on error) */
int gemfs_delete(const char *name);

/* Find file by name in parent (returns index or -1) */
int gemfs_find_in_dir(int parent_id, const char *name);

/* Find by full path? (Not yet implemented) */
int gemfs_find(
    const char *name); /* Searches ROOT only for now for backward compat */

/* Get file info */
int gemfs_count(void);
const char *gemfs_get_name(int index);
uint32_t gemfs_get_size(int index);
uint8_t gemfs_get_type(int index);
int8_t gemfs_get_parent(int index);

#endif /* GEMFS_H */
