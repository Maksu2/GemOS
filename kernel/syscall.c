#include "syscall.h"

#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "memory/paging.h"
#include "process.h"
#include "scheduler.h"

#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include "fs/gemfs.h"
#include "../include/gemos/console_abi.h"
#include <string.h>

#define SYSCALL_DEBUG_WRITE_MAX 256U
#define SYSCALL_CONSOLE_WRITE_CHUNK 128U
/* Longest SYS_file_write. A syscall runs with interrupts off from entry to
 * exit (AGENTS.md), so this also bounds how long the disk keeps them off. */
#define SYSCALL_FILE_WRITE_MAX (1024U * 1024U)

static gemos_console_cell_t console_present_cells[GEMOS_CONSOLE_MAX_CELLS];
static uint8_t syscall_file_buffer[GEMFS_BLOCK_SIZE]; /* one chunk of a read */

extern void isr128(void);

static process_t *syscall_current_process(void) {
  return scheduler_get_current_process();
}

int copy_from_user(void *destination, const void *user_source, size_t length) {
  process_t *process = syscall_current_process();

  if (length == 0) {
    return 1;
  }
  if (process == NULL || destination == NULL || user_source == NULL) {
    return 0;
  }
  if (!paging_is_user_range_mapped(process->as.page_directory,
                                   (uintptr_t)user_source, length, 0)) {
    return 0;
  }

  memcpy(destination, user_source, length);
  return 1;
}

int copy_to_user(void *user_destination, const void *source, size_t length) {
  process_t *process = syscall_current_process();

  if (length == 0) {
    return 1;
  }
  if (process == NULL || user_destination == NULL || source == NULL) {
    return 0;
  }
  if (!paging_is_user_range_mapped(process->as.page_directory,
                                   (uintptr_t)user_destination, length, 1)) {
    return 0;
  }

  memcpy(user_destination, source, length);
  return 1;
}

int copy_user_string(char *destination, const char *user_source, size_t max_length) {
  if (destination == NULL || user_source == NULL || max_length == 0) {
    return 0;
  }

  for (size_t i = 0; i < max_length; ++i) {
    char ch;

    if (!copy_from_user(&ch, user_source + i, 1)) {
      return 0;
    }
    destination[i] = ch;
    if (ch == '\0') {
      return 1;
    }
  }

  destination[max_length - 1] = '\0';
  return 1;
}

static uint32_t syscall_debug_write(uintptr_t user_buffer, size_t length) {
  char scratch[SYSCALL_DEBUG_WRITE_MAX + 1];

  if (length > SYSCALL_DEBUG_WRITE_MAX) {
    return (uint32_t)GEMOS_ERR_TOO_BIG;
  }
  if (!copy_from_user(scratch, (const void *)user_buffer, length)) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  scratch[length] = '\0';
  serial_print("[USER] ");
  serial_print(scratch);
  return (uint32_t)length;
}

static uint32_t syscall_console_open(uintptr_t user_title, uint32_t cols,
                                     uint32_t rows, uint32_t flags) {
  process_t *process = syscall_current_process();
  char title[GEMOS_CONSOLE_MAX_TITLE + 1];

  if (process == NULL) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  title[0] = '\0';
  if (user_title != 0 &&
      !copy_user_string(title, (const char *)user_title, sizeof(title))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  return (uint32_t)console_open(process->pid, title, cols, rows, flags);
}

static uint32_t syscall_console_write(uint32_t handle, uintptr_t user_buffer,
                                      size_t length) {
  process_t *process = syscall_current_process();
  char scratch[SYSCALL_CONSOLE_WRITE_CHUNK];
  size_t offset = 0;
  int total_written = 0;

  if (process == NULL || user_buffer == 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (length == 0) {
    return 0;
  }
  while (offset < length) {
    size_t chunk = length - offset;
    int write_result;

    if (chunk > sizeof(scratch)) {
      chunk = sizeof(scratch);
    }
    if (!copy_from_user(scratch, (const void *)(user_buffer + offset), chunk)) {
      return (uint32_t)GEMOS_ERR_FAULT;
    }

    write_result = console_write(process->pid, (int)handle, scratch, chunk);
    if (write_result < 0) {
      return (uint32_t)write_result;
    }

    total_written += write_result;
    offset += chunk;
  }

  return (uint32_t)total_written;
}

static uint32_t syscall_console_poll_event(uint32_t handle,
                                           uintptr_t user_event_ptr) {
  process_t *process = syscall_current_process();
  gemos_console_event_t event;
  int poll_result;

  if (process == NULL || user_event_ptr == 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  poll_result = console_poll_event(process->pid, (int)handle, &event);
  if (poll_result <= 0) {
    return (uint32_t)poll_result;
  }
  if (!copy_to_user((void *)user_event_ptr, &event, sizeof(event))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  return 1;
}

/* The two bytes before the return address are "int $0x80" (CD 80). */
static int syscall_can_restart(const registers_t *regs) {
  uint8_t insn[2];

  return regs->eip >= 2U &&
         copy_from_user(insn, (const void *)(uintptr_t)(regs->eip - 2U), 2) &&
         insn[0] == 0xCDU && insn[1] == 0x80U;
}

/*
 * SYS_console_wait_event: console_poll_event that sleeps while the queue is
 * empty. The process blocks with nothing on its kernel stack but the
 * syscall frame, whose EIP is moved back to the int $0x80: when an event
 * (process_wake) or the timeout (scheduler tick) wakes it, the syscall runs
 * again and returns the event, or 0 once the deadline has passed. A killed
 * process therefore never leaves kernel code half way.
 */
static void syscall_console_wait_event(registers_t *regs) {
  process_t *process = syscall_current_process();
  uintptr_t user_event_ptr = (uintptr_t)regs->ecx;
  uint32_t timeout_ms = regs->edx;
  gemos_console_event_t event;
  int poll_result;
  uint64_t now;

  if (process == NULL || user_event_ptr == 0) {
    regs->eax = (uint32_t)GEMOS_ERR_INVAL;
    return;
  }

  poll_result = console_poll_event(process->pid, (int)regs->ebx, &event);
  if (poll_result != 0) {
    process->waiting = 0;
    if (poll_result < 0) {
      regs->eax = (uint32_t)poll_result;
    } else if (!copy_to_user((void *)user_event_ptr, &event, sizeof(event))) {
      regs->eax = (uint32_t)GEMOS_ERR_FAULT;
    } else {
      regs->eax = 1;
    }
    return;
  }

  /* PIT ticks are milliseconds (PIT_FREQ = 1000), like SYS_ticks_ms. */
  now = timer_get_ticks();
  if (!process->waiting) {
    if (timeout_ms == 0) {
      regs->eax = 0;
      return;
    }
    process->waiting = 1;
    process->wait_deadline =
        timeout_ms == GEMOS_WAIT_FOREVER ? 0 : now + timeout_ms;
  } else if (process->wait_deadline != 0 && now >= process->wait_deadline) {
    process->waiting = 0;
    regs->eax = 0;
    return;
  }

  if (!syscall_can_restart(regs)) {
    process->waiting = 0;
    regs->eax = 0;
    return;
  }

  /* EAX still holds SYS_console_wait_event, EBX..EDX the arguments. */
  regs->eip -= 2U;
  scheduler_block_current(process->wait_deadline);
}

static uint32_t syscall_console_clear(uint32_t handle) {
  process_t *process = syscall_current_process();

  if (process == NULL) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  return (uint32_t)console_clear(process->pid, (int)handle);
}

static uint32_t syscall_console_present(uint32_t handle,
                                        uintptr_t user_frame_ptr) {
  process_t *process = syscall_current_process();
  gemos_console_frame_t frame;
  size_t cell_count;

  if (process == NULL || user_frame_ptr == 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (!copy_from_user(&frame, (const void *)user_frame_ptr, sizeof(frame))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }
  if (frame.cols == 0 || frame.rows == 0 ||
      frame.cols > GEMOS_CONSOLE_MAX_COLS || frame.rows > GEMOS_CONSOLE_MAX_ROWS) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (frame.cursor_row >= frame.rows || frame.cursor_col >= frame.cols) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  cell_count = (size_t)frame.cols * (size_t)frame.rows;
  if (cell_count > GEMOS_CONSOLE_MAX_CELLS) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (frame.cells == NULL) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  if (!copy_from_user(console_present_cells, frame.cells,
                      cell_count * sizeof(gemos_console_cell_t))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  return (uint32_t)console_present(process->pid, (int)handle, &frame,
                                   console_present_cells);
}

/* Copy a path from user memory. Unlike copy_user_string, a path that does
 * not fit is an error: cut short, it could name another file. */
static uint32_t copy_user_path(char path[GEMFS_PATH_MAX], uintptr_t user_path) {
  for (size_t i = 0; i < GEMFS_PATH_MAX; ++i) {
    if (!copy_from_user(&path[i], (const char *)user_path + i, 1)) {
      return (uint32_t)GEMOS_ERR_FAULT;
    }
    if (path[i] == '\0') {
      return (uint32_t)GEMOS_OK;
    }
  }
  return (uint32_t)GEMOS_ERR_INVAL;
}

static uint32_t syscall_file_error(int error) {
  switch (error) {
  case GEMFS_ERR_NOFS:
  case GEMFS_ERR_NOENT:
    return (uint32_t)GEMOS_ERR_NOENT;
  case GEMFS_ERR_TOOBIG:
  case GEMFS_ERR_NOSPC:
    return (uint32_t)GEMOS_ERR_TOO_BIG;
  case GEMFS_ERR_SOURCE:
    return (uint32_t)GEMOS_ERR_FAULT;
  case GEMFS_ERR_DENIED:
    return (uint32_t)GEMOS_ERR_DENIED;
  default:
    return (uint32_t)GEMOS_ERR_INVAL;
  }
}

/* Reads at most capacity - 1 bytes and ends them with a NUL. Returns the
 * number of bytes read. */
static uint32_t syscall_file_read(uintptr_t user_path, uintptr_t user_buffer,
                                  size_t capacity) {
  char path[GEMFS_PATH_MAX];
  gemfs_stat_t stat;
  uint32_t length;
  uint32_t done = 0;
  uint8_t terminator = 0;
  uint32_t status;
  int result;

  if (user_path == 0 || user_buffer == 0 || capacity == 0U) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  status = copy_user_path(path, user_path);
  if (status != (uint32_t)GEMOS_OK) {
    return status;
  }
  result = gemfs_stat(path, &stat);
  if (result != GEMFS_OK) {
    return syscall_file_error(result);
  }
  if (stat.type != GEMFS_TYPE_FILE) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  length = stat.size < capacity - 1U ? stat.size : (uint32_t)(capacity - 1U);
  while (done < length) {
    uint32_t chunk = length - done < sizeof(syscall_file_buffer)
                         ? length - done
                         : (uint32_t)sizeof(syscall_file_buffer);

    result = gemfs_read(stat.inode, done, syscall_file_buffer, chunk);
    if (result != (int)chunk) {
      return result < 0 ? syscall_file_error(result)
                        : (uint32_t)GEMOS_ERR_INVAL;
    }
    if (!copy_to_user((void *)(user_buffer + done), syscall_file_buffer,
                      chunk)) {
      return (uint32_t)GEMOS_ERR_FAULT;
    }
    done += chunk;
  }
  if (!copy_to_user((void *)(user_buffer + length), &terminator, 1)) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }
  return length;
}

/* GemFS reads the new contents block by block straight from the process */
static int syscall_user_source(void *ctx, uint32_t offset, void *dst,
                               uint32_t len) {
  uintptr_t user_buffer = *(const uintptr_t *)ctx;

  return copy_from_user(dst, (const void *)(user_buffer + offset), len) ? 0
                                                                        : -1;
}

static uint32_t syscall_file_write(uintptr_t user_path,
                                   uintptr_t user_buffer, size_t length) {
  char path[GEMFS_PATH_MAX];
  uint32_t status;
  int result;

  if (user_path == 0 || user_buffer == 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (length > SYSCALL_FILE_WRITE_MAX) {
    return (uint32_t)GEMOS_ERR_TOO_BIG;
  }
  status = copy_user_path(path, user_path);
  if (status != (uint32_t)GEMOS_OK) {
    return status;
  }

  /* programs and system files are read-only for processes */
  result = gemfs_write_user(path, (uint32_t)length, syscall_user_source,
                            &user_buffer);
  if (result < 0) {
    return syscall_file_error(result);
  }
  return (uint32_t)result;
}

void syscall_interrupt_handler(registers_t *regs) {
  switch (regs->eax) {
  case SYS_exit:
    /* the task no longer runs: isr_handler switches on the way out */
    scheduler_mark_current_zombie((int32_t)regs->ebx);
    break;
  case SYS_yield:
    regs->eax = 0;
    scheduler_request_yield();
    break;
  case SYS_debug_write:
    regs->eax = syscall_debug_write((uintptr_t)regs->ebx, (size_t)regs->ecx);
    break;
  case SYS_getpid:
    regs->eax = (uint32_t)scheduler_get_current_pid();
    break;
  case SYS_ticks_ms:
    regs->eax = (uint32_t)timer_get_ticks();
    break;
  case SYS_console_open:
    regs->eax = syscall_console_open((uintptr_t)regs->ebx, regs->ecx, regs->edx,
                                     regs->esi);
    break;
  case SYS_console_write:
    regs->eax = syscall_console_write(regs->ebx, (uintptr_t)regs->ecx,
                                      (size_t)regs->edx);
    break;
  case SYS_console_poll_event:
    regs->eax =
        syscall_console_poll_event(regs->ebx, (uintptr_t)regs->ecx);
    break;
  case SYS_console_wait_event:
    syscall_console_wait_event(regs);
    break;
  case SYS_console_clear:
    regs->eax = syscall_console_clear(regs->ebx);
    break;
  case SYS_console_present:
    regs->eax = syscall_console_present(regs->ebx, (uintptr_t)regs->ecx);
    break;
  case SYS_file_read:
    regs->eax = syscall_file_read((uintptr_t)regs->ebx, (uintptr_t)regs->ecx,
                                  (size_t)regs->edx);
    break;
  case SYS_file_write:
    regs->eax = syscall_file_write((uintptr_t)regs->ebx,
                                   (uintptr_t)regs->ecx, (size_t)regs->edx);
    break;
  default:
    regs->eax = (uint32_t)GEMOS_ERR_NOSYS;
    break;
  }
}

void syscall_init(void) {
  idt_set_gate(SYSCALL_VECTOR, (uint32_t)(uintptr_t)isr128, GDT_KERNEL_CS, 0xEE);
  register_interrupt_handler(SYSCALL_VECTOR, syscall_interrupt_handler);
  serial_print("[SYSCALL] int 0x80 ready\n");
}
