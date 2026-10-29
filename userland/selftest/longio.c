/*
 * LONGIO.ELF, self-test image only (make selftest).
 *
 * Long file syscalls. kernel/selftest.c slows down disk writes while this
 * runs, so a write of GEMOS_LONGIO_SIZE bytes takes about half a second,
 * and it checks what goes on meanwhile: the clock, other tasks, the GUI
 * loop, two more processes on the same files, a kill half way through a
 * write. The test number in the image (after GEMOS_SELFTEST_MARKER) says
 * what this copy does; see include/gemos/selftest_abi.h.
 */
#include <gemos/selftest_abi.h>
#include <gemos/user_api.h>

static volatile const struct {
  char marker[GEMOS_SELFTEST_MARKER_SIZE];
  uint32_t number;
} __attribute__((packed)) test = {GEMOS_SELFTEST_MARKER, 0};

static uint8_t data[GEMOS_LONGIO_SIZE + 1];

static int write_pattern(const char *path, uint32_t pattern) {
  for (uint32_t i = 0; i < GEMOS_LONGIO_SIZE; ++i) {
    data[i] = (uint8_t)GEMOS_LONGIO_BYTE(i, pattern);
  }
  return gemos_file_write(path, (const char *)data, GEMOS_LONGIO_SIZE) ==
         GEMOS_LONGIO_SIZE;
}

/* The pattern the whole file holds, 0 if it is not one whole pattern, -1
 * if the read failed or the size is wrong. */
static int32_t read_pattern(const char *path) {
  uint32_t pattern = 1;

  if (gemos_file_read(path, (char *)data, sizeof(data)) !=
      GEMOS_LONGIO_SIZE) {
    return -1;
  }
  while (pattern <= GEMOS_LONGIO_PATTERNS &&
         data[0] != (uint8_t)GEMOS_LONGIO_BYTE(0, pattern)) {
    pattern++;
  }
  if (pattern > GEMOS_LONGIO_PATTERNS) {
    return 0;
  }
  for (uint32_t i = 1; i < GEMOS_LONGIO_SIZE; ++i) {
    if (data[i] != (uint8_t)GEMOS_LONGIO_BYTE(i, pattern)) {
      return 0;
    }
  }
  return (int32_t)pattern;
}

static int timed_write(void) {
  int32_t start;

  for (uint32_t i = 0; i < GEMOS_LONGIO_SIZE; ++i) {
    data[i] = (uint8_t)GEMOS_LONGIO_BYTE(i, 1U);
  }
  start = gemos_ticks_ms();
  if (gemos_file_write(GEMOS_LONGIO_FILE_A, (const char *)data,
                       GEMOS_LONGIO_SIZE) != GEMOS_LONGIO_SIZE) {
    return -1;
  }
  return gemos_ticks_ms() - start;
}

int main(void) {
  int32_t pattern;

  switch (test.number) {
  case GEMOS_LONGIO_TIMED:
    return timed_write();
  case GEMOS_LONGIO_WRITER:
    for (uint32_t p = 2; p <= 5; ++p) {
      if (!write_pattern(GEMOS_LONGIO_FILE_A, p)) {
        return GEMOS_LONGIO_WRITE;
      }
    }
    return GEMOS_LONGIO_OK;
  case GEMOS_LONGIO_READER:
    for (uint32_t i = 0; i < GEMOS_LONGIO_READS; ++i) {
      pattern = read_pattern(GEMOS_LONGIO_FILE_A);
      if (pattern <= 0) {
        return pattern < 0 ? GEMOS_LONGIO_READ : GEMOS_LONGIO_TORN;
      }
    }
    return GEMOS_LONGIO_OK;
  case GEMOS_LONGIO_OTHER:
    if (!write_pattern(GEMOS_LONGIO_FILE_B, 6)) {
      return GEMOS_LONGIO_WRITE;
    }
    pattern = read_pattern(GEMOS_LONGIO_FILE_B);
    if (pattern != 6) {
      return pattern < 0 ? GEMOS_LONGIO_READ : GEMOS_LONGIO_WRONG;
    }
    return GEMOS_LONGIO_OK;
  case GEMOS_LONGIO_VICTIM:
    (void)write_pattern(GEMOS_LONGIO_FILE_C, 8);
    return GEMOS_LONGIO_SURVIVED;
  default:
    return GEMOS_LONGIO_BAD_TEST;
  }
}
