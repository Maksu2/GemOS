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
#define SYSCALL_FILE_NAME_MAX (GEMFS_MAX_FILENAME + 1U)

static uint32_t pending_resume_esp = 0;
static gemos_console_cell_t console_present_cells[GEMOS_CONSOLE_MAX_CELLS];
static uint8_t syscall_file_buffer[GEMFS_MAX_FILESIZE + 1U];

extern void isr128(void);

static process_t *syscall_current_process(void) {
  return scheduler_get_current_process();
}

uint32_t syscall_take_pending_resume_esp(void) {
  uint32_t resume_esp = pending_resume_esp;
  pending_resume_esp = 0;
  return resume_esp;
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

static uint32_t syscall_file_read(uintptr_t user_name_ptr,
                                  uintptr_t user_buffer_ptr,
                                  size_t user_capacity) {
  char name[SYSCALL_FILE_NAME_MAX];
  int read_result;
  size_t copy_length;

  if (user_name_ptr == 0 || user_buffer_ptr == 0 || user_capacity == 0U) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (user_capacity > sizeof(syscall_file_buffer)) {
    user_capacity = sizeof(syscall_file_buffer);
  }
  if (!copy_user_string(name, (const char *)user_name_ptr, sizeof(name))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  read_result =
      gemfs_read(name, (char *)syscall_file_buffer, (uint32_t)user_capacity);
  if (read_result < 0) {
    return (uint32_t)GEMOS_ERR_NOENT;
  }

  copy_length = (size_t)read_result + 1U;
  if (copy_length > user_capacity) {
    copy_length = user_capacity;
  }
  if (!copy_to_user((void *)user_buffer_ptr, syscall_file_buffer, copy_length)) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  return (uint32_t)read_result;
}

static uint32_t syscall_file_write(uintptr_t user_name_ptr,
                                   uintptr_t user_buffer_ptr, size_t length) {
  char name[SYSCALL_FILE_NAME_MAX];
  int write_result;

  if (user_name_ptr == 0 || user_buffer_ptr == 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }
  if (length > GEMFS_MAX_FILESIZE) {
    return (uint32_t)GEMOS_ERR_TOO_BIG;
  }
  if (!copy_user_string(name, (const char *)user_name_ptr, sizeof(name))) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }
  if (length > 0U &&
      !copy_from_user(syscall_file_buffer, (const void *)user_buffer_ptr,
                      length)) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }

  write_result = gemfs_write(name, (const char *)syscall_file_buffer,
                             (uint32_t)length);
  if (write_result < 0) {
    return (uint32_t)GEMOS_ERR_INVAL;
  }

  return (uint32_t)write_result;
}

void syscall_interrupt_handler(registers_t *regs) {
  pending_resume_esp = 0;

  switch (regs->eax) {
  case SYS_exit:
    scheduler_mark_current_zombie((int32_t)regs->ebx);
    pending_resume_esp = scheduler_switch_now((uint32_t)(uintptr_t)regs);
    break;
  case SYS_yield:
    regs->eax = 0;
    pending_resume_esp = scheduler_yield_now((uint32_t)(uintptr_t)regs);
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
