#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Initialize ATA Driver */
void ata_init(void);

/* Read/Write Sectors (LBA 28-bit) */
/* buf must be at least 512 bytes */
void ata_read_sector(uint32_t lba, uint8_t *buf);
void ata_write_sector(uint32_t lba, const uint8_t *buf);

#endif
