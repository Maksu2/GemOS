#include "tests.h"
#include "../drivers/serial.h"

void run_system_verification(void) {
  serial_print("\n[TEST] --- Starting System Verification (Phase 2.25) ---\n");

  /* 1. IDT Test: Breakpoint Exception */
  serial_print("[TEST] 1. Breakpoint Exception (INT 3)... ");
  /* This should be caught by ISR 3, logged, and return without halting */
  __asm__ volatile("int $3");
  serial_print("PASS (Execution continued)\n");

  /* 2. IRQ Test: IRQ0 Stub */
  serial_print("[TEST] 2. IRQ0 Stub Trigger (INT 32)... ");
  /* This simulates a timer interrupt. ISR 32 should log [IRQ] and send EOI. */
  __asm__ volatile("int $32");
  serial_print("PASS (Execution continued)\n");

  /* 3. General Stability Check */
  serial_print("[TEST] 3. General Stability... ");
  serial_print("PASS (System did not crash)\n");

  serial_print("[TEST] --- Verification Complete ---\n\n");
}
