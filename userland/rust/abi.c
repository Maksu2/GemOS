#include <gemos/user_api.h>
#include <gemos/fs_abi.h>
#include <gemos/paths.h>
#include <gemos/window_abi.h>
#include <string.h>

/* The Rust event shape is used as the destination of a kernel copy. */
_Static_assert(sizeof(gemos_console_event_t) == 16 &&
               __builtin_offsetof(gemos_console_event_t, type) == 0 &&
               __builtin_offsetof(gemos_console_event_t, key_code) == 4 &&
               __builtin_offsetof(gemos_console_event_t, character) == 8 &&
               __builtin_offsetof(gemos_console_event_t, modifiers) == 12,
               "Update the Rust console event layout");
_Static_assert(GEMOS_CONSOLE_EVENT_KEY == 1 &&
               GEMOS_CONSOLE_EVENT_CLOSE_REQUEST == 2 &&
               GEMOS_WAIT_FOREVER == 0xFFFFFFFFU && GEMOS_ERR_INVAL == -1,
               "Update the Rust ABI constants");

const uint32_t gemos_rust_key_escape = GEMOS_KEY_ESC;

/* The Rust SDK uses the same syscall numbers and calling convention as C apps. */
int32_t gemos_rust_console_open(const char *title, uint32_t cols,
                                uint32_t rows) {
  return gemos_console_open(title, cols, rows, 0);
}

int32_t gemos_rust_console_write(int32_t handle, const char *bytes,
                                 size_t length) {
  return gemos_console_write(handle, bytes, length);
}

int32_t gemos_rust_console_wait_event(int32_t handle,
                                      gemos_console_event_t *event,
                                      uint32_t timeout_ms) {
  return gemos_console_wait_event(handle, event, timeout_ms);
}

int32_t gemos_rust_file_read(const char *path, char *buffer, size_t capacity) {
  return gemos_file_read(path, buffer, capacity);
}

int32_t gemos_rust_file_write(const char *path, const char *bytes,
                              size_t length) {
  return gemos_file_write(path, bytes, length);
}

int32_t gemos_rust_debug_write(const char *bytes, size_t length) {
  return gemos_debug_write(bytes, length);
}

_Noreturn void gemos_rust_exit(int32_t status) {
  gemos_exit(status);
}

/* A single table supplies the mapped key codes to the Rust window SDK. */
const uint8_t gemos_rust_window_keys[] = {
    GEMOS_KEY_BACKSPACE, GEMOS_KEY_ENTER, GEMOS_KEY_ESC, GEMOS_KEY_UP,
    GEMOS_KEY_DOWN, GEMOS_KEY_LEFT, GEMOS_KEY_RIGHT, GEMOS_KEY_HOME,
    GEMOS_KEY_END, GEMOS_KEY_PAGE_UP, GEMOS_KEY_PAGE_DOWN, GEMOS_KEY_F2,
    GEMOS_KEY_DELETE,
};

_Static_assert(GEMOS_KEYMOD_CTRL == 1 && GEMOS_KEYMOD_ALT == 4,
               "Update the Rust key modifiers");
_Static_assert(GEMOS_WINDOW_LEFT_PRESS == 3 && GEMOS_WINDOW_RIGHT_PRESS == 4 &&
                   GEMOS_WINDOW_POINTER_MOVE == 5 && GEMOS_WINDOW_LEFT_RELEASE == 6 &&
                   GEMOS_WINDOW_RESIZE == 7 && GEMOS_WINDOW_MENU_ACTION == 8 &&
                   GEMOS_WINDOW_FS_CHANGED == 9,
               "Update the Rust window events");
_Static_assert(GEMOS_DRAW_LIMIT == 192 && sizeof(gemos_draw_command_t) == 156 &&
                   GEMOS_DRAW_FILL == 1 && GEMOS_DRAW_FRAME == 2 &&
                   GEMOS_DRAW_TEXT == 3 && GEMOS_DRAW_ICON == 4,
               "Update the Rust draw commands");
_Static_assert(GEMOS_ICON_FOLDER == 1 && GEMOS_ICON_TEXT == 2 &&
                   GEMOS_ICON_PROGRAM == 3 && GEMOS_ICON_FILE == 4,
               "Update the Rust icon IDs");
_Static_assert(GEMOS_MENU_LIMIT == 8 && sizeof(gemos_menu_item_t) == 36 &&
                   sizeof(gemos_window_menu_t) == 300,
               "Update the Rust menu ABI");
_Static_assert(GEMOS_DIRECTORY_PAGE == 48 && sizeof(gemos_directory_entry_t) == 64 &&
                   sizeof(gemos_directory_page_t) == 3084 &&
                   GEMOS_NAME_BYTES == 56 && GEMOS_PATH_BYTES == 128,
               "Update the Rust directory ABI");
_Static_assert(GEMOS_DIRECTORY == 2 && GEMOS_ERR_TOO_BIG == -4 &&
                   GEMOS_ERR_EXIST == -8 && GEMOS_ERR_NOFS == -9,
               "Update the Rust file constants");

int32_t gemos_rust_window_open(const char *title, uint32_t width, uint32_t height,
                               uint32_t icon) {
  return gemos_syscall4(SYS_window_open, (uintptr_t)title, width, height, icon);
}

int32_t gemos_rust_window_present(int32_t handle, const gemos_draw_command_t *commands,
                                  uint32_t count) {
  return gemos_syscall3(SYS_window_present, (uintptr_t)handle, (uintptr_t)commands, count);
}

int32_t gemos_rust_window_menu(int32_t handle, const gemos_window_menu_t *menu) {
  return gemos_syscall2(SYS_window_menu, (uintptr_t)handle, (uintptr_t)menu);
}

int32_t gemos_rust_text_width(const char *text, uint32_t size) {
  return gemos_syscall2(SYS_text_width, (uintptr_t)text, size);
}

int32_t gemos_rust_directory_read(const char *path, uint32_t first,
                                  gemos_directory_page_t *page) {
  return gemos_syscall3(SYS_directory_read, (uintptr_t)path, first, (uintptr_t)page);
}

int32_t gemos_rust_directory_create(const char *path) {
  return gemos_syscall1(SYS_directory_create, (uintptr_t)path);
}

int32_t gemos_rust_entry_rename(const char *path, const char *name) {
  return gemos_syscall2(SYS_entry_rename, (uintptr_t)path, (uintptr_t)name);
}

int32_t gemos_rust_open_path(const char *path) {
  return gemos_syscall1(SYS_open_path, (uintptr_t)path);
}

uint32_t gemos_rust_fs_generation(void) {
  return (uint32_t)gemos_syscall0(SYS_fs_generation);
}

const char *gemos_rust_home(void) {
  return GEMOS_HOME_PATH;
}

/* LLVM can lower byte-slice equality to bcmp on the freestanding target. */
int bcmp(const void *left, const void *right, size_t length) {
  return memcmp(left, right, length);
}
