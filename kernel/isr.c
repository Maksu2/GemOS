#include "isr.h"
#include "scheduler.h"
#include "syscall.h"
#include "../drivers/pic.h"
#include "../drivers/serial.h"
#include "idt.h"
#include <string.h>

/* Array of interrupt handlers */
isr_t interrupt_handlers[256];

/* External Assembly ISR stubs (Exceptions) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* External Assembly ISR stubs (IRQs) */
extern void isr32(void);
extern void isr33(void);
extern void isr34(void);
extern void isr35(void);
extern void isr36(void);
extern void isr37(void);
extern void isr38(void);
extern void isr39(void);
extern void isr40(void);
extern void isr41(void);
extern void isr42(void);
extern void isr43(void);
extern void isr44(void);
extern void isr45(void);
extern void isr46(void);
extern void isr47(void);
extern void isr128(void);

/* Exception messages */
char *exception_messages[] = {"Division By Zero",
                              "Debug",
                              "Non Maskable Interrupt",
                              "Breakpoint",
                              "Into Detected Overflow",
                              "Out of Bounds",
                              "Invalid Opcode",
                              "No Coprocessor",
                              "Double Fault",
                              "Coprocessor Segment Overrun",
                              "Bad TSS",
                              "Segment Not Present",
                              "Stack Fault",
                              "General Protection Fault",
                              "Page Fault",
                              "Unknown Interrupt",
                              "Coprocessor Fault",
                              "Alignment Check",
                              "Machine Check",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved"};

/* Initialize Interrupt Service Routines */
void init_isr(void) {
  /* Initialize IDT first */
  init_idt();

  /* Set gates for first 32 interrupts (CPU Exceptions) */
  idt_set_gate(0, (uint32_t)(uintptr_t)isr0, 0x08, 0x8E);
  idt_set_gate(1, (uint32_t)(uintptr_t)isr1, 0x08, 0x8E);
  idt_set_gate(2, (uint32_t)(uintptr_t)isr2, 0x08, 0x8E);
  idt_set_gate(3, (uint32_t)(uintptr_t)isr3, 0x08, 0x8E);
  idt_set_gate(4, (uint32_t)(uintptr_t)isr4, 0x08, 0x8E);
  idt_set_gate(5, (uint32_t)(uintptr_t)isr5, 0x08, 0x8E);
  idt_set_gate(6, (uint32_t)(uintptr_t)isr6, 0x08, 0x8E);
  idt_set_gate(7, (uint32_t)(uintptr_t)isr7, 0x08, 0x8E);
  idt_set_gate(8, (uint32_t)(uintptr_t)isr8, 0x08, 0x8E);
  idt_set_gate(9, (uint32_t)(uintptr_t)isr9, 0x08, 0x8E);
  idt_set_gate(10, (uint32_t)(uintptr_t)isr10, 0x08, 0x8E);
  idt_set_gate(11, (uint32_t)(uintptr_t)isr11, 0x08, 0x8E);
  idt_set_gate(12, (uint32_t)(uintptr_t)isr12, 0x08, 0x8E);
  idt_set_gate(13, (uint32_t)(uintptr_t)isr13, 0x08, 0x8E);
  idt_set_gate(14, (uint32_t)(uintptr_t)isr14, 0x08, 0x8E);
  idt_set_gate(15, (uint32_t)(uintptr_t)isr15, 0x08, 0x8E);
  idt_set_gate(16, (uint32_t)(uintptr_t)isr16, 0x08, 0x8E);
  idt_set_gate(17, (uint32_t)(uintptr_t)isr17, 0x08, 0x8E);
  idt_set_gate(18, (uint32_t)(uintptr_t)isr18, 0x08, 0x8E);
  idt_set_gate(19, (uint32_t)(uintptr_t)isr19, 0x08, 0x8E);
  idt_set_gate(20, (uint32_t)(uintptr_t)isr20, 0x08, 0x8E);
  idt_set_gate(21, (uint32_t)(uintptr_t)isr21, 0x08, 0x8E);
  idt_set_gate(22, (uint32_t)(uintptr_t)isr22, 0x08, 0x8E);
  idt_set_gate(23, (uint32_t)(uintptr_t)isr23, 0x08, 0x8E);
  idt_set_gate(24, (uint32_t)(uintptr_t)isr24, 0x08, 0x8E);
  idt_set_gate(25, (uint32_t)(uintptr_t)isr25, 0x08, 0x8E);
  idt_set_gate(26, (uint32_t)(uintptr_t)isr26, 0x08, 0x8E);
  idt_set_gate(27, (uint32_t)(uintptr_t)isr27, 0x08, 0x8E);
  idt_set_gate(28, (uint32_t)(uintptr_t)isr28, 0x08, 0x8E);
  idt_set_gate(29, (uint32_t)(uintptr_t)isr29, 0x08, 0x8E);
  idt_set_gate(30, (uint32_t)(uintptr_t)isr30, 0x08, 0x8E);
  idt_set_gate(31, (uint32_t)(uintptr_t)isr31, 0x08, 0x8E);

  /* Set gates for IRQs (32-47) */
  idt_set_gate(32, (uint32_t)(uintptr_t)isr32, 0x08, 0x8E);
  idt_set_gate(33, (uint32_t)(uintptr_t)isr33, 0x08, 0x8E);
  idt_set_gate(34, (uint32_t)(uintptr_t)isr34, 0x08, 0x8E);
  idt_set_gate(35, (uint32_t)(uintptr_t)isr35, 0x08, 0x8E);
  idt_set_gate(36, (uint32_t)(uintptr_t)isr36, 0x08, 0x8E);
  idt_set_gate(37, (uint32_t)(uintptr_t)isr37, 0x08, 0x8E);
  idt_set_gate(38, (uint32_t)(uintptr_t)isr38, 0x08, 0x8E);
  idt_set_gate(39, (uint32_t)(uintptr_t)isr39, 0x08, 0x8E);
  idt_set_gate(40, (uint32_t)(uintptr_t)isr40, 0x08, 0x8E);
  idt_set_gate(41, (uint32_t)(uintptr_t)isr41, 0x08, 0x8E);
  idt_set_gate(42, (uint32_t)(uintptr_t)isr42, 0x08, 0x8E);
  idt_set_gate(43, (uint32_t)(uintptr_t)isr43, 0x08, 0x8E);
  idt_set_gate(44, (uint32_t)(uintptr_t)isr44, 0x08, 0x8E);
  idt_set_gate(45, (uint32_t)(uintptr_t)isr45, 0x08, 0x8E);
  idt_set_gate(46, (uint32_t)(uintptr_t)isr46, 0x08, 0x8E);
  idt_set_gate(47, (uint32_t)(uintptr_t)isr47, 0x08, 0x8E);

  memset(&interrupt_handlers, 0, sizeof(isr_t) * 256);
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
  interrupt_handlers[n] = handler;
}

static uint32_t isr_read_cr2(void) {
  uint32_t cr2;
  __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
  return cr2;
}

static int isr_can_kill_user_task(uint32_t int_no) {
  switch (int_no) {
  case 0:
  case 6:
  case 10:
  case 11:
  case 12:
  case 13:
  case 14:
    return 1;
  default:
    return 0;
  }
}

uint32_t isr_handler(registers_t *regs) {
  uint32_t fault_cr2 = 0;
  int from_user = ((regs->cs & 0x3U) == 0x3U);

  /* Log output */
  if (regs->int_no >= PIC1_OFFSET && regs->int_no <= PIC2_OFFSET + 7) {
    /* Silent normal hardware IRQs for clean serial output. */
  } else if (regs->int_no == SYSCALL_VECTOR) {
    /* Silent normal syscall traffic; user-visible output comes from handlers. */
  } else {
    serial_print("[ISR] Interrupt: ");
    serial_print_dec(regs->int_no);
    serial_print("\n");
  }

  if (regs->int_no < PIC1_OFFSET) {
    /* Special Case for Breakpoint (INT 3) - Continue */
    if (regs->int_no == 3) {
      serial_print("  [INFO] Breakpoint hit (continuing)\n");
      return 0;
    }

    if (regs->int_no == 14) {
      fault_cr2 = isr_read_cr2();
    }

    if (from_user && isr_can_kill_user_task(regs->int_no)) {
      serial_print("[USERFAULT] pid=");
      serial_print_dec((uint32_t)scheduler_get_current_pid());
      serial_print(" vec=");
      serial_print_dec(regs->int_no);
      serial_print(" eip=0x");
      serial_print_hex(regs->eip);
      serial_print(" err=0x");
      serial_print_hex(regs->err_code);
      if (regs->int_no == 14) {
        serial_print(" cr2=0x");
        serial_print_hex(fault_cr2);
      }
      serial_print("\n");

      scheduler_mark_current_fault(regs->int_no, regs->err_code, fault_cr2);
      return scheduler_switch_now((uint32_t)(uintptr_t)regs);
    }

    /* CPU Exception */
    serial_print("\n[PANIC] CPU Exception: ");
    serial_print(exception_messages[regs->int_no]);
    serial_print("\n");
    serial_print("  EIP: 0x");
    serial_print_hex(regs->eip);
    serial_print(" Error Code: ");
    serial_print_dec(regs->err_code);
    serial_print("\n  CS: 0x");
    serial_print_hex(regs->cs);
    serial_print(" EFLAGS: 0x");
    serial_print_hex(regs->eflags);
    if (regs->int_no == 14) {
      serial_print("\n  CR2: 0x");
      serial_print_hex(fault_cr2);
    }
    serial_print("\n");

    serial_print("System Halted.\n");
    for (;;) {
      __asm__ volatile("hlt");
    }
  }

  /* Handle IRQ EOI */
  if (regs->int_no >= PIC1_OFFSET && regs->int_no <= PIC2_OFFSET + 7) {
    pic_send_eoi(regs->int_no - PIC1_OFFSET);
  }

  if (interrupt_handlers[regs->int_no] != 0) {
    isr_t handler = interrupt_handlers[regs->int_no];
    handler(regs);
  }

  return syscall_take_pending_resume_esp();
}
