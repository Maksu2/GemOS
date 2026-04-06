/**
 * CMOS RTC Driver
 *
 * Reads real-time clock from CMOS chip via I/O ports 0x70 (index) and 0x71
 * (data). The RTC stores time in BCD format by default.
 *
 * Port 0x70 bit 7 controls NMI: we preserve it (set to 0 = NMI enabled).
 */

#include "rtc.h"
#include <io.h>

/* CMOS Registers */
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define RTC_REG_SECONDS 0x00
#define RTC_REG_MINUTES 0x02
#define RTC_REG_HOURS 0x04
#define RTC_REG_STATUS_B 0x0B

static uint8_t cmos_read(uint8_t reg) {
  /* Bit 7 of port 0x70 controls NMI. Keep it 0 (NMI enabled). */
  outb(CMOS_ADDR, reg & 0x7F);
  return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static int rtc_is_bcd(void) {
  uint8_t status_b = cmos_read(RTC_REG_STATUS_B);
  /* Bit 2 of Status Register B: 0 = BCD, 1 = Binary */
  return !(status_b & 0x04);
}

uint8_t rtc_get_seconds(void) {
  uint8_t val = cmos_read(RTC_REG_SECONDS);
  return rtc_is_bcd() ? bcd_to_bin(val) : val;
}

uint8_t rtc_get_minutes(void) {
  uint8_t val = cmos_read(RTC_REG_MINUTES);
  return rtc_is_bcd() ? bcd_to_bin(val) : val;
}

uint8_t rtc_get_hours(void) {
  uint8_t val = cmos_read(RTC_REG_HOURS);
  if (rtc_is_bcd()) {
    /* Handle 12-hour mode with PM bit */
    uint8_t pm = val & 0x80;
    val = bcd_to_bin(val & 0x7F);
    if (pm && val < 12)
      val += 12;
    if (!pm && val == 12)
      val = 0;
    return val;
  }
  return val;
}
