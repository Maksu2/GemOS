/*
 * FILETEST.ELF, self-test image only (make selftest).
 *
 * The file syscalls as a process sees them: a file larger than 64 KB in a
 * directory, one two levels further down, reading a program, and the
 * writes a process may not do (programs and system files are read-only).
 * kernel/selftest.c writes this program to GemFS and starts it from there;
 * its image is larger than 64 KB (filetest_data.S), so the loader is tested
 * too. The exit code is 0 or the first step that failed
 * (include/gemos/selftest_abi.h); the kernel then checks the disk.
 */
#include <gemos/selftest_abi.h>
#include <gemos/user_api.h>

extern const uint8_t filetest_table[GEMOS_FILETEST_SIZE];

static char readback[GEMOS_FILETEST_SIZE + 1];

static int same(const char *a, const char *b, uint32_t length) {
  for (uint32_t i = 0; i < length; ++i) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

static int refused(const char *path) {
  static const char text[] = "not a program";

  return gemos_file_write(path, text, sizeof(text) - 1U) == GEMOS_ERR_DENIED;
}

int main(void) {
  static const char note[] = GEMOS_FILETEST_NOTE_TEXT;
  const uint32_t note_length = sizeof(note) - 1U;

  for (uint32_t i = 0; i < GEMOS_FILETEST_SIZE; ++i) {
    if (filetest_table[i] != (uint8_t)GEMOS_FILETEST_BYTE(i)) {
      return GEMOS_FILETEST_IMAGE;
    }
  }

  if (gemos_file_write(GEMOS_FILETEST_BIG, (const char *)filetest_table,
                       GEMOS_FILETEST_SIZE) != GEMOS_FILETEST_SIZE) {
    return GEMOS_FILETEST_WRITE;
  }
  if (gemos_file_read(GEMOS_FILETEST_BIG, readback, sizeof(readback)) !=
          GEMOS_FILETEST_SIZE ||
      !same(readback, (const char *)filetest_table, GEMOS_FILETEST_SIZE)) {
    return GEMOS_FILETEST_READ;
  }

  if (gemos_file_write(GEMOS_FILETEST_NOTE, note, note_length) !=
          (int32_t)note_length ||
      gemos_file_read(GEMOS_FILETEST_NOTE, readback, sizeof(readback)) !=
          (int32_t)note_length ||
      !same(readback, note, note_length)) {
    return GEMOS_FILETEST_NOTE_FAILED;
  }

  /* programs can be read: capacity 5 gives the 4 magic bytes and a NUL */
  if (gemos_file_read("UTERM.ELF", readback, 5) != 4 ||
      !same(readback, "\x7F" "ELF", 4)) {
    return GEMOS_FILETEST_READ_PROGRAM;
  }
  if (!refused("UTERM.ELF") || !refused("/UTERM.ELF")) {
    return GEMOS_FILETEST_OVERWRITE;
  }
  if (!refused("uterm.elf")) {
    return GEMOS_FILETEST_OTHER_CASE;
  }
  if (!refused("UTERM.ELF/")) {
    return GEMOS_FILETEST_SLASH;
  }
  if (!refused("/fstest/NEW.ELF")) {
    return GEMOS_FILETEST_NEW_PROGRAM;
  }

  if (gemos_file_write("/no-such-directory/file.txt", note, note_length) !=
      GEMOS_ERR_NOENT) {
    return GEMOS_FILETEST_NO_DIR;
  }
  return GEMOS_FILETEST_OK;
}
