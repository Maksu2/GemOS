/*
 * Power off
 *
 * ACPI soft-off through the PM1a control register at the ports QEMU and
 * Bochs use: 0x604 (QEMU's PIIX4 and ICH9 power management, where SeaBIOS
 * puts it) and 0xB004 (Bochs, QEMU before 2.0). Full ACPI (the register
 * from the FADT, SLP_TYP from \_S5 in the DSDT) would also work on real
 * hardware, but the tables usually lie above 32 MB, which the kernel does
 * not map.
 */

#ifndef POWER_H
#define POWER_H

/* Turn the machine off. Interrupts stay off from here on; if the machine
 * is still running after both attempts, it halts with a line in the log
 * (the caller has already said on the screen that it is safe to turn the
 * computer off). Never returns. */
void power_off(void) __attribute__((noreturn));

#endif /* POWER_H */
