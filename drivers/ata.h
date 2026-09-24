#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/*
 * ATA PIO driver (LBA28). ata_init() probes the four legacy positions
 * (primary/secondary channel, master/slave) with IDENTIFY. Every wait on
 * the controller is bounded, so a missing or broken disk returns an error
 * instead of hanging the kernel.
 */

#define ATA_MAX_DEVICES 4

enum {
  ATA_OK = 0,
  ATA_ERR_NO_DEVICE = -1, /* no disk at that index */
  ATA_ERR_RANGE = -2,     /* LBA or count outside the disk */
  ATA_ERR_TIMEOUT = -3,   /* the controller stayed busy */
  ATA_ERR_DEVICE = -4,    /* the disk reported ERR or DF */
};

typedef struct {
  int present;
  uint16_t io_base;   /* 0x1F0 primary, 0x170 secondary */
  uint16_t ctrl_base; /* 0x3F6 primary, 0x376 secondary */
  uint8_t slave;      /* 0 master, 1 slave */
  uint32_t sectors;   /* LBA28 capacity from IDENTIFY */
  char model[41];
} ata_device_t;

/* Probe all positions (safe to call when no disk is attached). */
void ata_init(void);

/* Device 0..ATA_MAX_DEVICES-1 (primary master, primary slave, secondary
 * master, secondary slave) or NULL if nothing answered there. */
const ata_device_t *ata_get_device(int index);

/* Read/write count (1..256) 512-byte sectors. Returns ATA_OK or an ATA_ERR_*
 * code. */
int ata_read(int index, uint32_t lba, uint32_t count, void *buf);
int ata_write(int index, uint32_t lba, uint32_t count, const void *buf);

#endif
