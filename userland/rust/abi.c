#include <gemos/user_api.h>

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
