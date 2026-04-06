#include "ata.h"
#include "../include/io.h"
#include "serial.h"

#define ATA_DATA 0x1F0
#define ATA_FEATURES 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE_head 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7

#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

static void ata_wait_bsy(void) {
  while (inb(ATA_STATUS) & 0x80)
    ;
}

static void ata_wait_drq(void) {
  while (!(inb(ATA_STATUS) & 0x08))
    ;
}

void ata_init(void) { serial_print("[ATA] Driver Initialized (PIO Mode)\n"); }

void ata_read_sector(uint32_t lba, uint8_t *buf) {
  uint32_t io = ATA_DATA;

  /* Select Drive (Master) + LBA High 4 bits */
  outb(ATA_DRIVE_head, 0xE0 | ((lba >> 24) & 0x0F));
  /* NULL Byte? No, wait a bit? */
  outb(ATA_SECTOR_COUNT, 1);
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_READ_PIO);

  ata_wait_bsy();
  ata_wait_drq();

  for (int i = 0; i < 256; i++) {
    uint16_t tmp = inw(io);
    buf[i * 2] = (uint8_t)tmp;
    buf[i * 2 + 1] = (uint8_t)(tmp >> 8);
  }
}

void ata_write_sector(uint32_t lba, const uint8_t *buf) {
  uint32_t io = ATA_DATA;

  outb(ATA_DRIVE_head, 0xE0 | ((lba >> 24) & 0x0F));
  outb(ATA_SECTOR_COUNT, 1);
  outb(ATA_LBA_LO, (uint8_t)lba);
  outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
  outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
  outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

  ata_wait_bsy();
  /* Wait for DRQ before writing? Yes. */
  ata_wait_drq();

  for (int i = 0; i < 256; i++) {
    uint16_t tmp = buf[i * 2] | (buf[i * 2 + 1] << 8);
    outw(io, tmp);
  }

  ata_wait_bsy();
  /* Flush Cache */
  outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
  ata_wait_bsy();
}
