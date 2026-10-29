/*
 * GemOS freestanding <string.h>
 *
 * The subset of the C string functions the kernel uses, implemented in
 * lib/string.c. memcpy, memmove, memset and memcmp must stay: GCC may emit
 * calls to them even in freestanding code.
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

void *memset(void *ptr, int value, size_t num);
void *memcpy(void *destination, const void *source, size_t num);
void *memmove(void *destination, const void *source, size_t num);
int memcmp(const void *s1, const void *s2, size_t num);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t num);
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t num);
size_t strlen(const char *str);
char *strcat(char *destination, const char *source);
char *strrchr(const char *str, int character);

#endif /* STRING_H */
