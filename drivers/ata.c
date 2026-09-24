#include "ata.h"
#include "../include/io.h"
#include "serial.h"

#include <stddef.h>

/* Task file registers, relative to io_base */
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_SECTOR_COUNT 2
#define ATA_REG_LBA_LO 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HI 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7 /* read */
#define ATA_REG_COMMAND 7 /* write */

/* Status bits */
#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_BSY 0x80

#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_LBA28_LIMIT 0x10000000U

/* Upper bound for every wait: status reads, each about 1 us on a real ISA
 * bus, so roughly a second. Interrupts may be off (syscalls), so the bound
 * is a count and not the tick counter. */
#define ATA_TIMEOUT_POLLS 1000000U

static ata_device_t ata_devices[ATA_MAX_DEVICES];

static uint8_t ata_alt_status(const ata_device_t *dev) {
  /* the alternate status register does not acknowledge an interrupt */
  return inb(dev->ctrl_base);
}

/* 400 ns for the drive to put its status on the bus after a select */
static void ata_delay_400ns(const ata_device_t *dev) {
  for (int i = 0; i < 4; ++i) {
    (void)ata_alt_status(dev);
  }
}

static int ata_wait_not_busy(const ata_device_t *dev) {
  for (uint32_t i = 0; i < ATA_TIMEOUT_POLLS; ++i) {
    uint8_t status = ata_alt_status(dev);

    if (status == 0xFF) {
      return ATA_ERR_NO_DEVICE; /* floating bus */
    }
    if ((status & ATA_SR_BSY) == 0) {
      return (status & (ATA_SR_ERR | ATA_SR_DF)) ? ATA_ERR_DEVICE : ATA_OK;
    }
  }
  return ATA_ERR_TIMEOUT;
}

/* Wait until the drive has a sector ready (DRQ) or reports an error. */
static int ata_wait_data(const ata_device_t *dev) {
  for (uint32_t i = 0; i < ATA_TIMEOUT_POLLS; ++i) {
    uint8_t status = ata_alt_status(dev);

    if (status == 0xFF) {
      return ATA_ERR_NO_DEVICE;
    }
    if (status & ATA_SR_BSY) {
      continue;
    }
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
      return ATA_ERR_DEVICE;
    }
    if (status & ATA_SR_DRQ) {
      return ATA_OK;
    }
  }
  return ATA_ERR_TIMEOUT;
}

static void ata_select(const ata_device_t *dev, uint8_t lba_bits) {
  outb(dev->io_base + ATA_REG_DRIVE,
       (uint8_t)(0xE0 | (dev->slave << 4) | (lba_bits & 0x0F)));
  ata_delay_400ns(dev);
}

static void ata_copy_model(ata_device_t *dev, const uint16_t *identify) {
  int end = 40;

  /* words 27-46, two characters per word, high byte first */
  for (int i = 0; i < 20; ++i) {
    dev->model[i * 2] = (char)(identify[27 + i] >> 8);
    dev->model[i * 2 + 1] = (char)(identify[27 + i] & 0xFF);
  }
  while (end > 0 && (dev->model[end - 1] == ' ' || dev->model[end - 1] == 0)) {
    end--;
  }
  dev->model[end] = '\0';
}

static int ata_identify(ata_device_t *dev) {
  uint16_t identify[256];
  uint8_t status;

  if (inb(dev->io_base + ATA_REG_STATUS) == 0xFF) {
    return 0; /* nothing on this channel */
  }

  ata_select(dev, 0);
  outb(dev->io_base + ATA_REG_SECTOR_COUNT, 0);
  outb(dev->io_base + ATA_REG_LBA_LO, 0);
  outb(dev->io_base + ATA_REG_LBA_MID, 0);
  outb(dev->io_base + ATA_REG_LBA_HI, 0);
  outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

  status = inb(dev->io_base + ATA_REG_STATUS);
  if (status == 0 || status == 0xFF) {
    return 0; /* no drive at this position */
  }
  switch (ata_wait_not_busy(dev)) {
  case ATA_ERR_NO_DEVICE:
  case ATA_ERR_TIMEOUT:
    return 0;
  default:
    break; /* ERR here may be an ATAPI device aborting IDENTIFY */
  }
  /* ATAPI and SATA devices put a signature here and abort IDENTIFY */
  if (inb(dev->io_base + ATA_REG_LBA_MID) != 0 ||
      inb(dev->io_base + ATA_REG_LBA_HI) != 0) {
    return 0;
  }
  if (ata_wait_data(dev) != ATA_OK) {
    return 0;
  }

  for (int i = 0; i < 256; ++i) {
    identify[i] = inw(dev->io_base + ATA_REG_DATA);
  }

  /* word 49 bit 9: LBA supported; words 60-61: LBA28 sector count */
  if ((identify[49] & (1U << 9)) == 0) {
    return 0;
  }
  dev->sectors = (uint32_t)identify[60] | ((uint32_t)identify[61] << 16);
  ata_copy_model(dev, identify);
  return dev->sectors != 0;
}

void ata_init(void) {
  static const uint16_t io_bases[2] = {0x1F0, 0x170};
  static const uint16_t ctrl_bases[2] = {0x3F6, 0x376};
  static const char *names[ATA_MAX_DEVICES] = {
      "primary master", "primary slave", "secondary master",
      "secondary slave"};
  int found = 0;

  for (int i = 0; i < ATA_MAX_DEVICES; ++i) {
    ata_device_t *dev = &ata_devices[i];

    dev->present = 0;
    dev->io_base = io_bases[i / 2];
    dev->ctrl_base = ctrl_bases[i / 2];
    dev->slave = (uint8_t)(i % 2);
    dev->sectors = 0;
    dev->model[0] = '\0';

    if (ata_identify(dev)) {
      dev->present = 1;
      found++;
      serial_print("[ATA] ");
      serial_print(names[i]);
      serial_print(": ");
      serial_print(dev->model);
      serial_print(", ");
      serial_print_dec(dev->sectors / 2048U);
      serial_print(" MB\n");
    }
  }

  if (found == 0) {
    serial_print("[ATA] No ATA disks found\n");
  }
}

const ata_device_t *ata_get_device(int index) {
  if (index < 0 || index >= ATA_MAX_DEVICES || !ata_devices[index].present) {
    return NULL;
  }
  return &ata_devices[index];
}

static int ata_start(const ata_device_t *dev, uint32_t lba, uint32_t count,
                     uint8_t command) {
  int result;

  ata_select(dev, (uint8_t)(lba >> 24));
  result = ata_wait_not_busy(dev);
  if (result != ATA_OK) {
    return result;
  }
  outb(dev->io_base + ATA_REG_SECTOR_COUNT, (uint8_t)count); /* 256 -> 0 */
  outb(dev->io_base + ATA_REG_LBA_LO, (uint8_t)lba);
  outb(dev->io_base + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
  outb(dev->io_base + ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
  outb(dev->io_base + ATA_REG_COMMAND, command);
  ata_delay_400ns(dev);
  return ATA_OK;
}

static int ata_check(int index, uint32_t lba, uint32_t count,
                     const ata_device_t **dev) {
  *dev = ata_get_device(index);
  if (*dev == NULL) {
    return ATA_ERR_NO_DEVICE;
  }
  if (count == 0 || count > 256 || lba >= ATA_LBA28_LIMIT ||
      lba >= (*dev)->sectors || count > (*dev)->sectors - lba) {
    return ATA_ERR_RANGE;
  }
  return ATA_OK;
}

static int ata_fail(const ata_device_t *dev, const char *what, uint32_t lba,
                    int result) {
  serial_print("[ATA] ");
  serial_print(what);
  serial_print(" failed at LBA ");
  serial_print_dec(lba);
  serial_print(result == ATA_ERR_TIMEOUT ? ": timeout\n" : ": error 0x");
  if (result != ATA_ERR_TIMEOUT) {
    serial_print_hex(inb(dev->io_base + ATA_REG_ERROR));
    serial_print("\n");
  }
  return result;
}

int ata_read(int index, uint32_t lba, uint32_t count, void *buf) {
  const ata_device_t *dev;
  uint16_t *words = (uint16_t *)buf;
  int result = ata_check(index, lba, count, &dev);

  if (result != ATA_OK) {
    return result;
  }
  result = ata_start(dev, lba, count, ATA_CMD_READ_PIO);
  if (result != ATA_OK) {
    return ata_fail(dev, "read", lba, result);
  }
  for (uint32_t s = 0; s < count; ++s) {
    result = ata_wait_data(dev);
    if (result != ATA_OK) {
      return ata_fail(dev, "read", lba + s, result);
    }
    for (int i = 0; i < 256; ++i) {
      *words++ = inw(dev->io_base + ATA_REG_DATA);
    }
  }
  return ATA_OK;
}

int ata_write(int index, uint32_t lba, uint32_t count, const void *buf) {
  const ata_device_t *dev;
  const uint16_t *words = (const uint16_t *)buf;
  int result = ata_check(index, lba, count, &dev);

  if (result != ATA_OK) {
    return result;
  }
  result = ata_start(dev, lba, count, ATA_CMD_WRITE_PIO);
  if (result != ATA_OK) {
    return ata_fail(dev, "write", lba, result);
  }
  for (uint32_t s = 0; s < count; ++s) {
    result = ata_wait_data(dev);
    if (result != ATA_OK) {
      return ata_fail(dev, "write", lba + s, result);
    }
    for (int i = 0; i < 256; ++i) {
      outw(dev->io_base + ATA_REG_DATA, *words++);
    }
  }

  /* the data must be on the disk before the next command */
  result = ata_wait_not_busy(dev);
  if (result == ATA_OK) {
    outb(dev->io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_delay_400ns(dev);
    result = ata_wait_not_busy(dev);
  }
  if (result != ATA_OK) {
    return ata_fail(dev, "write", lba, result);
  }
  return ATA_OK;
}
