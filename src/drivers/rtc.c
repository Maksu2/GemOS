#include "io.h"

// CMOS Ports
#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

// Registers
// 0x00 Seconds
// 0x02 Minutes
// 0x04 Hours
// ...
// 0x0B Status Register B

int get_update_in_progress_flag() {
  outb(CMOS_ADDRESS, 0x0A);
  return (inb(CMOS_DATA) & 0x80);
}
unsigned char get_rtc_register(int reg) {
  outb(CMOS_ADDRESS, reg);
  return inb(CMOS_DATA);
}

// BCD to Binary
// (val & 0x0F) + ((val / 16) * 10)
unsigned char bcd2bin(unsigned char bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
void rtc_get_time(int *hour, int *min, int *sec) {
  // Wait for update (Timeout added to prevent freeze)
  int timeout = 100000;
  while (get_update_in_progress_flag() && timeout-- > 0)
    ;

  if (timeout <= 0) {
    // RTC Hardware Fault or Timeout, return fallback
    *hour = 0;
    *min = 0;
    *sec = 0;
    return;
  }

  unsigned char s = get_rtc_register(0x00);
  unsigned char m = get_rtc_register(0x02);
  unsigned char h = get_rtc_register(0x04);

  // Convert BCD if necessary
  // Check Status Register B bit 2 (0x04) = Binary Mode?
  // Usually PC RTC is BCD.
  unsigned char regB = get_rtc_register(0x0B);

  if (!(regB & 0x04)) {
    s = bcd2bin(s);
    m = bcd2bin(m);
    h = bcd2bin(h);
  }

  // 24 Hour check (RegB bit 1)
  if (!(regB & 0x02) && (h & 0x80)) {
    h = ((h & 0x7F) + 12) % 24;
  }

  *hour = (int)h;
  *min = (int)m;
  *sec = (int)s;
}
