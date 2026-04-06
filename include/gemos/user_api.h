#ifndef GEMOS_USER_API_H
#define GEMOS_USER_API_H

#include "console_abi.h"
#include "syscall_abi.h"

#include <stddef.h>
#include <stdint.h>

static inline int32_t gemos_syscall0(uint32_t number) {
  int32_t result;
  __asm__ volatile("int $0x80" : "=a"(result) : "a"(number) : "memory");
  return result;
}

static inline int32_t gemos_syscall1(uint32_t number, uintptr_t arg0) {
  int32_t result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg0)
                   : "memory");
  return result;
}

static inline int32_t gemos_syscall2(uint32_t number, uintptr_t arg0,
                                     uintptr_t arg1) {
  int32_t result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg0), "c"(arg1)
                   : "memory");
  return result;
}

static inline int32_t gemos_syscall3(uint32_t number, uintptr_t arg0,
                                     uintptr_t arg1, uintptr_t arg2) {
  int32_t result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
                   : "memory");
  return result;
}

static inline int32_t gemos_syscall4(uint32_t number, uintptr_t arg0,
                                     uintptr_t arg1, uintptr_t arg2,
                                     uintptr_t arg3) {
  int32_t result;
  __asm__ volatile("int $0x80"
                   : "=a"(result)
                   : "a"(number), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3)
                   : "memory");
  return result;
}

static inline void gemos_exit(int32_t status) {
  (void)gemos_syscall1(SYS_exit, (uintptr_t)status);
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static inline int32_t gemos_yield(void) {
  return gemos_syscall0(SYS_yield);
}

static inline int32_t gemos_debug_write(const char *buffer, size_t length) {
  return gemos_syscall2(SYS_debug_write, (uintptr_t)buffer, length);
}

static inline int32_t gemos_getpid(void) {
  return gemos_syscall0(SYS_getpid);
}

static inline int32_t gemos_ticks_ms(void) {
  return gemos_syscall0(SYS_ticks_ms);
}

static inline int32_t gemos_console_open(const char *title, uint32_t cols,
                                         uint32_t rows, uint32_t flags) {
  return gemos_syscall4(SYS_console_open, (uintptr_t)title, cols, rows, flags);
}

static inline int32_t gemos_console_write(int32_t handle, const char *buffer,
                                          size_t length) {
  return gemos_syscall3(SYS_console_write, (uintptr_t)handle, (uintptr_t)buffer,
                        length);
}

static inline int32_t gemos_console_poll_event(int32_t handle,
                                               gemos_console_event_t *event) {
  return gemos_syscall2(SYS_console_poll_event, (uintptr_t)handle,
                        (uintptr_t)event);
}

static inline int32_t gemos_console_clear(int32_t handle) {
  return gemos_syscall1(SYS_console_clear, (uintptr_t)handle);
}

#endif /* GEMOS_USER_API_H */
