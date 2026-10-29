/*
 * Power off (see power.h)
 */

#include "power.h"
#include "pit.h"
#include "serial.h"
#include <io.h>

#define PM1A_CNT_QEMU 0x604   /* QEMU PIIX4/ICH9 PM base 0x600 + 4 */
#define PM1A_CNT_BOCHS 0xB004 /* Bochs, QEMU before 2.0 */
#define PM1_SLP_EN 0x2000     /* SLP_EN with SLP_TYP 0: soft off */

/* Time for the machine to act on the write; the PIT stopwatch also runs
 * with interrupts off */
#define POWER_WAIT_MS 200

static void power_wait(void) {
  pit_stopwatch_t watch;

  pit_stopwatch_start(&watch);
  while (pit_stopwatch_ms(&watch) < POWER_WAIT_MS) {
  }
}

void power_off(void) {
  serial_print("[POWER] Powering off\n");
  serial_drain();
  __asm__ volatile("cli");

  outw(PM1A_CNT_QEMU, PM1_SLP_EN);
  power_wait();
  outw(PM1A_CNT_BOCHS, PM1_SLP_EN);
  power_wait();

  serial_print("[POWER] Still running: no ACPI power off here, halted\n");
  serial_drain();
  for (;;) {
    __asm__ volatile("hlt");
  }
}
