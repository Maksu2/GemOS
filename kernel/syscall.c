#include "syscall.h"

#include "console.h"
#include "fpu.h"
#include "gdt.h"
#include "idt.h"
#include "include/irq.h"
#include "memory/paging.h"
#include "process.h"
#include "scheduler.h"
#include "../apps/textedit/utextedit_launcher.h"

#include "../drivers/pit.h"
#include "../drivers/serial.h"
#include "fs/gemfs.h"
#include "gfx/font/font.h"
#include "../include/gemos/console_abi.h"
#include <gemos/fs_abi.h>
#include <gemos/paths.h>
#include <gemos/window_abi.h>
#include <string.h>

#define SYSCALL_DEBUG_WRITE_MAX 256U
#define SYSCALL_CONSOLE_WRITE_CHUNK 128U
/* Longest SYS_file_write: a write keeps the disk until it is done. */
#define SYSCALL_FILE_WRITE_MAX (1024U * 1024U)

static gemos_console_cell_t console_present_cells[GEMOS_CONSOLE_MAX_CELLS];
/* These syscalls never give up the CPU between the copy and the commit. */
static gemos_draw_command_t window_commands[GEMOS_DRAW_LIMIT];

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

/* GemFS hands the file over block by block, straight into the process.
 * Between blocks other tasks may run, and this process may be killed:
 * then the read stops at the next block. */
static int syscall_user_sink(void *ctx, uint32_t offset, const void *src,
                             uint32_t len) {
  uintptr_t user_buffer = *(const uintptr_t *)ctx;

  if (scheduler_kill_pending()) {
    return -1;
  }
  return copy_to_user((void *)(user_buffer + offset), src, len) ? 0 : -1;
}

/* Reads at most capacity - 1 bytes and ends them with a NUL. Returns the
 * number of bytes read. */
static uint32_t syscall_file_read(uintptr_t user_path, uintptr_t user_buffer,
                                  size_t capacity) {
  char path[GEMFS_PATH_MAX];
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
  result = gemfs_read_file(path, (uint32_t)(capacity - 1U), syscall_user_sink,
                           &user_buffer);
  if (result < 0) {
    return syscall_file_error(result);
  }
  if (!copy_to_user((void *)(user_buffer + (uint32_t)result), &terminator,
                    1)) {
    return (uint32_t)GEMOS_ERR_FAULT;
  }
  return (uint32_t)result;
}

/* GemFS reads the new contents block by block straight from the process. A
 * process killed in the meantime abandons the write at the next block: the
 * old contents stay. */
static int syscall_user_source(void *ctx, uint32_t offset, void *dst,
                               uint32_t len) {
  uintptr_t user_buffer = *(const uintptr_t *)ctx;

  if (scheduler_kill_pending()) {
    return -1;
  }
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

static bool path_ends_in(const char *path, const char *extension) {
  size_t length = strlen(path);
  size_t tail = strlen(extension);
  if (length < tail) return false;
  path += length - tail;
  while (*extension) {
    char ch = *path++;
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    if (ch != *extension++) return false;
  }
  return true;
}

static int syscall_window_open(const char *title_ptr, uint32_t width,
                               uint32_t height, uint32_t icon) {
  char title[GEMOS_CONSOLE_MAX_TITLE + 1];
  if (!copy_user_string(title, title_ptr, sizeof(title))) {
    return GEMOS_ERR_FAULT;
  }
  return console_open_window(scheduler_get_current_pid(), title, width, height, icon);
}

static int syscall_window_present(int handle, const void *commands, uint32_t count) {
  if (count > GEMOS_DRAW_LIMIT) {
    return GEMOS_ERR_TOO_BIG;
  }
  if (!copy_from_user(window_commands, commands, count * sizeof(window_commands[0]))) {
    return GEMOS_ERR_FAULT;
  }
  return console_present_window(scheduler_get_current_pid(), handle, window_commands, count);
}

static int syscall_window_menu(int handle, const void *request) {
  gemos_window_menu_t menu;
  if (!copy_from_user(&menu, request, sizeof(menu))) {
    return GEMOS_ERR_FAULT;
  }
  return console_window_menu(scheduler_get_current_pid(), handle, &menu);
}

static int syscall_text_width(const char *text_ptr, uint32_t size) {
  char text[GEMOS_DRAW_TEXT_BYTES];
  if (size < 10 || size > 24) {
    return GEMOS_ERR_INVAL;
  }
  if (!copy_user_string(text, text_ptr, sizeof(text))) {
    return GEMOS_ERR_FAULT;
  }
  /* The font engine computes with floats. In a syscall the FPU holds the
   * process's state: keep it aside and measure from a clean state. */
  static fpu_state_t process_fpu;
  static fpu_state_t clean_fpu;
  fpu_save(&process_fpu);
  fpu_init_state(&clean_fpu);
  fpu_restore(&clean_fpu);
  int width = font_text_width(text, (int)size);
  fpu_restore(&process_fpu);
  return width;
}

static int directory_error(int error) {
  if (error == GEMFS_ERR_EXIST) return GEMOS_ERR_EXIST;
  if (error == GEMFS_ERR_NOFS) return GEMOS_ERR_NOFS;
  return (int)syscall_file_error(error);
}

typedef struct {
  uint32_t skip;
  gemos_directory_page_t *page;
} directory_read_t;

static void directory_entry(void *ctx, const gemfs_entry_t *entry) {
  directory_read_t *read = ctx;
  if (read->skip) {
    --read->skip;
    return;
  }
  gemos_directory_page_t *page = read->page;
  if (page->count < GEMOS_DIRECTORY_PAGE) {
    gemos_directory_entry_t *out = &page->entries[page->count++];
    out->inode = entry->inode;
    out->type = entry->type;
    strcpy(out->name, entry->name);
  }
}

static int syscall_directory_read(uintptr_t path_ptr, uint32_t first, void *out) {
  char path[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(path, path_ptr);
  if (result != GEMOS_OK) return result;

  /* Per-call storage: the process may sleep while GemFS is busy. */
  gemos_directory_page_t page = {0};
  directory_read_t read = {first, &page};
  result = gemfs_list(path, directory_entry, &read, &page.generation);
  if (result < 0) return directory_error(result);
  page.total = (uint32_t)result;
  return copy_to_user(out, &page, sizeof(page)) ? GEMOS_OK : GEMOS_ERR_FAULT;
}

static int syscall_directory_create(uintptr_t path_ptr) {
  char path[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(path, path_ptr);
  if (result != GEMOS_OK) return result;
  result = gemfs_mkdir(path);
  return result == GEMFS_OK ? GEMOS_OK : directory_error(result);
}

/* The Home directory keeps its name and stays */
static bool path_is_home(const char *path) {
  if (strcmp(path[0] == '/' ? path + 1 : path, GEMOS_HOME_PATH + 1) != 0) {
    return false;
  }
  gemfs_stat_t stat;
  return gemfs_stat(path, &stat) == GEMFS_OK && stat.type == GEMFS_TYPE_DIR;
}

static int syscall_entry_delete(uintptr_t path_ptr) {
  char path[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(path, path_ptr);
  if (result != GEMOS_OK) return result;
  if (path_is_home(path)) return GEMOS_ERR_DENIED;
  result = gemfs_delete_user(path);
  return result == GEMFS_OK ? GEMOS_OK : directory_error(result);
}

static int syscall_entry_copy(uintptr_t source_ptr, uintptr_t target_ptr) {
  char source[GEMFS_PATH_MAX];
  char target[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(source, source_ptr);
  if (result != GEMOS_OK) return result;
  result = (int)copy_user_path(target, target_ptr);
  if (result != GEMOS_OK) return result;
  result = gemfs_copy_user(source, target);
  return result == GEMFS_OK ? GEMOS_OK : directory_error(result);
}

static int syscall_entry_rename(uintptr_t path_ptr, uintptr_t name_ptr) {
  char path[GEMFS_PATH_MAX];
  char name[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(path, path_ptr);
  if (result != GEMOS_OK) return result;
  result = (int)copy_user_path(name, name_ptr);
  if (result != GEMOS_OK) return result;
  /* As with SYS_file_write, processes neither change programs nor make them. */
  if (path_ends_in(path, ".elf") || path_ends_in(name, ".elf")) {
    return GEMOS_ERR_DENIED;
  }
  if (path_is_home(path)) {
    return GEMOS_ERR_DENIED;
  }
  result = gemfs_rename(path, name);
  return result == GEMFS_OK ? GEMOS_OK : directory_error(result);
}


static int syscall_open_path(uintptr_t path_ptr) {
  char path[GEMFS_PATH_MAX];
  int result = (int)copy_user_path(path, path_ptr);
  if (result != GEMOS_OK) return result;
  if (path_ends_in(path, ".txt") || path_ends_in(path, ".gemtext")) {
    return utextedit_launcher_spawn(path) >= 0 ? GEMOS_OK : GEMOS_ERR_BUSY;
  }
  if (path_ends_in(path, ".elf")) {
    return process_spawn_user_from_file(path) >= 0 ? GEMOS_OK : GEMOS_ERR_INVAL;
  }
  return GEMOS_ERR_INVAL;
}

/*
 * The gate turns interrupts off; a syscall runs with them on, so the clock
 * and the input devices keep working during a long one. An IRQ returns to
 * the syscall (kernel code is never preempted, see kernel/scheduler.c), and
 * state shared with IRQ handlers is changed only inside irq_save(). The
 * way out of the interrupt, where the scheduler may switch tasks, runs with
 * interrupts off again.
 */
void syscall_interrupt_handler(registers_t *regs) {
  irq_enable();
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
  case SYS_window_open:
    regs->eax = syscall_window_open((const char *)regs->ebx, regs->ecx, regs->edx, regs->esi);
    break;
  case SYS_window_present:
    regs->eax = syscall_window_present((int)regs->ebx, (const void *)regs->ecx, regs->edx);
    break;
  case SYS_window_menu:
    regs->eax = syscall_window_menu((int)regs->ebx, (const void *)regs->ecx);
    break;
  case SYS_text_width:
    regs->eax = syscall_text_width((const char *)regs->ebx, regs->ecx);
    break;
  case SYS_directory_read:
    regs->eax = syscall_directory_read(regs->ebx, regs->ecx, (void *)regs->edx);
    break;
  case SYS_directory_create:
    regs->eax = syscall_directory_create(regs->ebx);
    break;
  case SYS_entry_rename:
    regs->eax = syscall_entry_rename(regs->ebx, regs->ecx);
    break;
  case SYS_open_path:
    regs->eax = syscall_open_path(regs->ebx);
    break;
  case SYS_fs_generation:
    regs->eax = gemfs_generation();
    break;
  case SYS_entry_delete:
    regs->eax = syscall_entry_delete(regs->ebx);
    break;
  case SYS_entry_copy:
    regs->eax = syscall_entry_copy(regs->ebx, regs->ecx);
    break;
  default:
    regs->eax = (uint32_t)GEMOS_ERR_NOSYS;
    break;
  }
  if (scheduler_kill_pending()) {
    /* killed while it was in here (process_kill_pid): it does not go back
     * to Ring 3 */
    process_t *process = syscall_current_process();

    if (process != NULL && process->state != PROC_ZOMBIE) {
      scheduler_mark_current_zombie(process->exit_code);
    }
  }
  irq_disable();
}

void syscall_init(void) {
  idt_set_gate(SYSCALL_VECTOR, (uint32_t)(uintptr_t)isr128, GDT_KERNEL_CS, 0xEE);
  register_interrupt_handler(SYSCALL_VECTOR, syscall_interrupt_handler);
  serial_print("[SYSCALL] int 0x80 ready\n");
}
