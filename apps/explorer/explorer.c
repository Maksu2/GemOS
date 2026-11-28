#include "explorer.h"

#include "../../kernel/app/app_manager.h"
#include "../../kernel/fs/gemfs.h"
#include "../../kernel/gfx/icons.h"
#include "../../kernel/process.h"
#include <gemos/paths.h>
#include <stddef.h>

extern uint8_t _binary_explorer_image_bin_start[];
extern uint8_t _binary_explorer_image_bin_end[];

int explorer_launch(void) {
  const uint8_t *image = _binary_explorer_image_bin_start;
  size_t size = (size_t)(_binary_explorer_image_bin_end - image);
  return process_spawn_user_image("EXPLORER.ELF", image, size);
}

static void explorer_open(void) { (void)explorer_launch(); }

void explorer_init(void) {
  static app_t explorer = {
      .name = "Files",
      .icon = &icon_folder,
      .open = explorer_open,
  };
  gemfs_stat_t home;
  if (gemfs_available() && gemfs_stat(GEMOS_HOME_PATH, &home) == GEMFS_ERR_NOENT) {
    (void)gemfs_mkdir(GEMOS_HOME_PATH);
  }
  app_register(&explorer);
}
