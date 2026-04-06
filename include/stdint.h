/*
 * GemOS Standard Integer Types
 *
 * Minimal stdint.h replacement for freestanding environment.
 */

#ifndef STDINT_H
#define STDINT_H

/* Exact-width integer types */
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

/* Pointer-sized integer types */
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;

/* Size type */
typedef uint32_t size_t;
typedef int32_t ssize_t;

/* Limits */
#define INT8_MIN (-128)
#define INT8_MAX 127
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647

#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Boolean type - use defines to avoid C23 conflict */
#ifndef __bool_true_false_are_defined
#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif

#endif /* STDINT_H */
