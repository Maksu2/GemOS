#include "term_util.h"

size_t term_strlen(const char *text) {
  size_t length = 0;

  if (text == 0) {
    return 0;
  }

  while (text[length] != '\0') {
    length++;
  }

  return length;
}

int term_streq(const char *left, const char *right) {
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

int term_starts_with(const char *text, const char *prefix) {
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

void term_copy(char *destination, size_t capacity, const char *source) {
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

void term_copy_n(char *destination, size_t capacity, const char *source,
                 size_t count) {
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

void term_u32_to_text(uint32_t value, char *buffer, size_t capacity) {
  char digits[11];
  size_t count = 0;
  size_t out = 0;

  if (buffer == 0 || capacity == 0) {
    return;
  }

  if (value == 0) {
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
