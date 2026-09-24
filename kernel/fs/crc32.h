#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32 (IEEE 802.3), the same as zlib.crc32 in tools/mkgemfs */
uint32_t crc32(const void *data, size_t length);

#endif /* CRC32_H */
