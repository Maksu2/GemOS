#ifndef GEMOS_USERLAND_COMMON_FORMAT_H
#define GEMOS_USERLAND_COMMON_FORMAT_H

#include <stddef.h>
#include <stdint.h>

static inline size_t gemos_strlen(const char *text) {
  size_t length = 0;

  if (text == 0) {
    return 0;
  }

  while (text[length] != '\0') {
    length++;
  }

  return length;
}

static inline int gemos_streq(const char *left, const char *right) {
  size_t index = 0;

  if (left == 0 || right == 0) {
    return 0;
  }

  while (left[index] != '\0' && right[index] != '\0') {
    if (left[index] != right[index]) {
      return 0;
    }
    index++;
  }

  return left[index] == right[index];
}

static inline int gemos_starts_with(const char *text, const char *prefix) {
  size_t index = 0;

  if (text == 0 || prefix == 0) {
    return 0;
  }

  while (prefix[index] != '\0') {
    if (text[index] != prefix[index]) {
      return 0;
    }
    index++;
  }

  return 1;
}

static inline void gemos_copy(char *destination, size_t capacity,
                              const char *source) {
  size_t index = 0;

  if (destination == 0 || capacity == 0) {
    return;
  }

  if (source == 0) {
    destination[0] = '\0';
    return;
  }

  while (source[index] != '\0' && index + 1U < capacity) {
    destination[index] = source[index];
    index++;
  }

  destination[index] = '\0';
}

static inline void gemos_copy_n(char *destination, size_t capacity,
                                const char *source, size_t count) {
  size_t index = 0;

  if (destination == 0 || capacity == 0) {
    return;
  }

  if (source == 0) {
    destination[0] = '\0';
    return;
  }

  while (index < count && source[index] != '\0' && index + 1U < capacity) {
    destination[index] = source[index];
    index++;
  }

  destination[index] = '\0';
}

static inline void gemos_u32_to_text(uint32_t value, char *buffer,
                                     size_t capacity) {
  char digits[11];
  size_t count = 0;
  size_t out = 0;

  if (buffer == 0 || capacity == 0) {
    return;
  }

  if (value == 0U) {
    if (capacity > 1U) {
      buffer[0] = '0';
      buffer[1] = '\0';
    } else {
      buffer[0] = '\0';
    }
    return;
  }

  while (value > 0U && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  }

  while (count > 0U && out + 1U < capacity) {
    count--;
    buffer[out++] = digits[count];
  }

  buffer[out] = '\0';
}

static inline void gemos_format_uptime(uint32_t total_seconds, char *buffer,
                                       size_t capacity) {
  char hours[16];
  char line[32];
  uint32_t hour_value = total_seconds / 3600U;
  uint32_t minute_value = (total_seconds / 60U) % 60U;
  uint32_t second_value = total_seconds % 60U;
  size_t offset = 0;

  if (buffer == 0 || capacity == 0U) {
    return;
  }

  gemos_u32_to_text(hour_value, hours, sizeof(hours));
  gemos_copy(line, sizeof(line), hours);
  offset = gemos_strlen(line);

  if (offset + 6U >= sizeof(line)) {
    gemos_copy(buffer, capacity, line);
    return;
  }

  line[offset++] = ':';
  line[offset++] = (char)('0' + (minute_value / 10U));
  line[offset++] = (char)('0' + (minute_value % 10U));
  line[offset++] = ':';
  line[offset++] = (char)('0' + (second_value / 10U));
  line[offset++] = (char)('0' + (second_value % 10U));
  line[offset] = '\0';

  gemos_copy(buffer, capacity, line);
}

#endif /* GEMOS_USERLAND_COMMON_FORMAT_H */
