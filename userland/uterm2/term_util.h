#ifndef UTERM2_TERM_UTIL_H
#define UTERM2_TERM_UTIL_H

#include "../common/format.h"

static inline size_t term_strlen(const char *text) {
  return gemos_strlen(text);
}

static inline int term_streq(const char *left, const char *right) {
  return gemos_streq(left, right);
}

static inline int term_starts_with(const char *text, const char *prefix) {
  return gemos_starts_with(text, prefix);
}

static inline void term_copy(char *destination, size_t capacity,
                             const char *source) {
  gemos_copy(destination, capacity, source);
}

static inline void term_copy_n(char *destination, size_t capacity,
                               const char *source, size_t count) {
  gemos_copy_n(destination, capacity, source, count);
}

static inline void term_u32_to_text(uint32_t value, char *buffer,
                                    size_t capacity) {
  gemos_u32_to_text(value, buffer, capacity);
}

#endif /* UTERM2_TERM_UTIL_H */
