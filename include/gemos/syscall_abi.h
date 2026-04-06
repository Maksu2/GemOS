#ifndef GEMOS_SYSCALL_ABI_H
#define GEMOS_SYSCALL_ABI_H

#define GEMOS_SYSCALL_VECTOR 0x80U

enum {
  SYS_exit = 0,
  SYS_yield = 1,
  SYS_debug_write = 2,
  SYS_getpid = 3,
  SYS_ticks_ms = 4,
  SYS_console_open = 5,
  SYS_console_write = 6,
  SYS_console_poll_event = 7,
  SYS_console_clear = 8,
};

enum {
  GEMOS_OK = 0,
  GEMOS_ERR_INVAL = -1,
  GEMOS_ERR_FAULT = -2,
  GEMOS_ERR_NOSYS = -3,
  GEMOS_ERR_TOO_BIG = -4,
  GEMOS_ERR_BUSY = -5,
  GEMOS_ERR_NOENT = -6,
};

#endif /* GEMOS_SYSCALL_ABI_H */
