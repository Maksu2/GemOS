/*
 * GemOS Standard Definitions
 *
 * Minimal stddef.h replacement for freestanding environment.
 */

#ifndef STDDEF_H
#define STDDEF_H

#include <stdint.h>

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Offset of member in structure */
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif /* STDDEF_H */
