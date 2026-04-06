#include <gemos/console_abi.h>
#include <gemos/user_api.h>

#include <stddef.h>
#include <stdint.h>

#define UTERM_MAX_LINE 128

static int32_t console_handle = -1;
static char command_buffer[UTERM_MAX_LINE];
static size_t command_length = 0;

static size_t uterm_strlen(const char *text) {
  size_t length = 0;

  while (text[length] != '\0') {
    length++;
  }

  return length;
}

static int uterm_streq(const char *left, const char *right) {
  size_t index = 0;

  while (left[index] != '\0' && right[index] != '\0') {
    if (left[index] != right[index]) {
      return 0;
    }
    index++;
  }

  return left[index] == right[index];
}

static int uterm_starts_with(const char *text, const char *prefix) {
  size_t index = 0;

  while (prefix[index] != '\0') {
    if (text[index] != prefix[index]) {
      return 0;
    }
    index++;
  }

  return 1;
}

static void uterm_write(const char *text) {
  gemos_console_write(console_handle, text, uterm_strlen(text));
}

static void uterm_write_char(char ch) {
  gemos_console_write(console_handle, &ch, 1);
}

static void uterm_write_u32(uint32_t value) {
  char digits[11];
  size_t count = 0;

  if (value == 0) {
    uterm_write("0");
    return;
  }

  while (value > 0 && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  }

  while (count > 0) {
    count--;
    uterm_write_char(digits[count]);
  }
}

static void uterm_prompt(void) { uterm_write("> "); }

static int uterm_run_command(void) {
  command_buffer[command_length] = '\0';

  if (command_length == 0) {
    uterm_prompt();
    return 1;
  }

  if (uterm_streq(command_buffer, "help")) {
    uterm_write("help  echo  pid  ticks  clear  exit\n");
  } else if (uterm_starts_with(command_buffer, "echo ")) {
    uterm_write(command_buffer + 5);
    uterm_write("\n");
  } else if (uterm_streq(command_buffer, "pid")) {
    uterm_write("pid=");
    uterm_write_u32((uint32_t)gemos_getpid());
    uterm_write("\n");
  } else if (uterm_streq(command_buffer, "ticks")) {
    uterm_write("ticks=");
    uterm_write_u32((uint32_t)gemos_ticks_ms());
    uterm_write("\n");
  } else if (uterm_streq(command_buffer, "clear")) {
    gemos_console_clear(console_handle);
  } else if (uterm_streq(command_buffer, "exit")) {
    return 0;
  } else {
    uterm_write("unknown command\n");
  }

  command_length = 0;
  uterm_prompt();
  return 1;
}

static int uterm_handle_key(uint32_t character) {
  char ch = (char)(uint8_t)character;

  if (character == GEMOS_KEY_ENTER) {
    uterm_write("\n");
    return uterm_run_command();
  }
  if (character == GEMOS_KEY_BACKSPACE) {
    if (command_length > 0) {
      command_length--;
      uterm_write_char('\b');
    }
    return 1;
  }
  if (ch >= 0x20 && ch <= 0x7E) {
    if (command_length + 1 < sizeof(command_buffer)) {
      command_buffer[command_length++] = ch;
      uterm_write_char(ch);
    }
  }

  return 1;
}

int main(void) {
  gemos_console_event_t event;
  int32_t result;

  console_handle = gemos_console_open("User Terminal", 80, 25, 0);
  if (console_handle < 0) {
    static const char open_failed[] = "UTERM failed to open console\n";
    gemos_debug_write(open_failed, sizeof(open_failed) - 1U);
    return 1;
  }

  uterm_write("GemOS User Terminal\n");
  uterm_write("Type 'help' for commands.\n");
  uterm_prompt();

  for (;;) {
    result = gemos_console_poll_event(console_handle, &event);
    if (result < 0) {
      return 2;
    }
    if (result == 0) {
      gemos_yield();
      continue;
    }
    if (event.type != GEMOS_CONSOLE_EVENT_KEY) {
      gemos_yield();
      continue;
    }
    if (!uterm_handle_key(event.character)) {
      return 0;
    }
  }
}
