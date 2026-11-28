#ifndef GEMOS_FS_ABI_H
#define GEMOS_FS_ABI_H

#include <stdint.h>

#define GEMOS_NAME_BYTES 56U
#define GEMOS_PATH_BYTES 128U
#define GEMOS_DIRECTORY_PAGE 48U
#define GEMOS_FILE 1U
#define GEMOS_DIRECTORY 2U

typedef struct {
  uint32_t inode;
  uint32_t type;
  char name[GEMOS_NAME_BYTES];
} gemos_directory_entry_t;

typedef struct {
  uint32_t total;
  uint32_t generation;
  uint32_t count;
  gemos_directory_entry_t entries[GEMOS_DIRECTORY_PAGE];
} gemos_directory_page_t;

#endif
