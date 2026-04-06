/**
 * CMOS RTC Driver
 *
 * Reads real-time clock from CMOS ports 0x70/0x71.
 * BCD-to-binary conversion included.
 */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* Read current time from CMOS RTC */
uint8_t rtc_get_seconds(void);
uint8_t rtc_get_minutes(void);
uint8_t rtc_get_hours(void);

#endif /* RTC_H */
