/*
 * GemOS String Utilities
 *
 * Basic string manipulation functions (no libc dependency).
 */

#include "string.h"

/* Fill memory with a constant byte - optimized with rep stosd */
void *memset(void *dest, int c, size_t n) {
  /* Fast path: use rep stosd for 4-byte aligned fills */
  uint8_t byte_val = (uint8_t)c;
  uint32_t dword_val = (uint32_t)byte_val | ((uint32_t)byte_val << 8) |
                       ((uint32_t)byte_val << 16) | ((uint32_t)byte_val << 24);

  size_t dwords = n / 4;
  size_t remainder = n % 4;

  if (dwords > 0) {
    __asm__ volatile("rep stosl"
                     : "+D"(dest), "+c"(dwords)
                     : "a"(dword_val)
                     : "memory");
  }

  /* Handle remaining bytes */
  uint8_t *d = (uint8_t *)dest;
  while (remainder--) {
    *d++ = byte_val;
  }

  return dest;
}

/* Copy memory area - optimized with rep movsd */
void *memcpy(void *dest, const void *src, size_t n) {
  void *ret = dest;

  /* Fast path: use rep movsd for 4-byte block copies */
  size_t dwords = n / 4;
  size_t remainder = n % 4;

  if (dwords > 0) {
    __asm__ volatile("rep movsl"
                     : "+D"(dest), "+S"(src), "+c"(dwords)
                     :
                     : "memory");
  }

  /* Handle remaining bytes */
  if (remainder > 0) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (remainder--) {
      *d++ = *s++;
    }
  }

  return ret;
}

/* Copy memory area (handles overlapping) */
void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }
  return dest;
}

/* Compare memory areas */
int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;

  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

/* Get string length */
size_t strlen(const char *str) {
  size_t len = 0;
  while (*str++) {
    len++;
  }
  return len;
}

/* Copy string */
char *strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++))
    ;
  return dest;
}

/* Copy string with limit */
char *strncpy(char *dest, const char *src, size_t n) {
  char *d = dest;
  while (n && (*d++ = *src++)) {
    n--;
  }
  while (n--) {
    *d++ = '\0';
  }
  return dest;
}

/* Compare strings */
int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Compare strings with limit */
int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0) {
    return 0;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Concatenate strings */
char *strcat(char *dest, const char *src) {
  char *d = dest;
  while (*d) {
    d++;
  }
  while ((*d++ = *src++))
    ;
  return dest;
}
