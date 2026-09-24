#include "isr.h"
#include "gdt.h"
#include "memory/kstack.h"
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
                              "SIMD Floating-Point Exception",
                              "Virtualization Exception",
                              "Control Protection Exception",
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
  /* int3 and into are user instructions (DPL 3 gates): from Ring 3 they
   * raise #BP and #OF, which end the process like any other exception. */
  idt_set_gate(3, (uint32_t)(uintptr_t)isr3, 0x08, 0xEE);
  idt_set_gate(4, (uint32_t)(uintptr_t)isr4, 0x08, 0xEE);
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

static uint32_t isr_read_cr0(void) {
  uint32_t value;
  __asm__ volatile("mov %%cr0, %0" : "=r"(value));
  return value;
}

static uint32_t isr_read_cr2(void) {
  uint32_t value;
  __asm__ volatile("mov %%cr2, %0" : "=r"(value));
  return value;
}

static uint32_t isr_read_cr3(void) {
  uint32_t value;
  __asm__ volatile("mov %%cr3, %0" : "=r"(value));
  return value;
}

static uint32_t isr_read_cr4(void) {
  uint32_t value;
  __asm__ volatile("mov %%cr4, %0" : "=r"(value));
  return value;
}

/* Exceptions a Ring 3 program can cause. NMI (2), double fault (8) and
 * machine check (18) report hardware or kernel trouble, whatever ran. */
static int isr_is_process_fault(uint32_t int_no) {
  return int_no < 32 && int_no != 2 && int_no != 8 && int_no != 18;
}

static void isr_print_reg(const char *name, uint32_t value) {
  serial_print(" ");
  serial_print(name);
  serial_print("=");
  serial_print_hex(value);
}

/* Kernel panic: everything the CPU saved, then halt. */
static void isr_panic(const registers_t *regs, uint32_t cr2) {
  int from_user = (regs->cs & 0x3U) == 0x3U;
  /* a Ring 0 frame ends at EFLAGS: the old ESP is right after it */
  uint32_t esp = from_user ? regs->useresp
                           : (uint32_t)(uintptr_t)&regs->useresp;
  int pid = scheduler_get_current_pid();

  serial_print("\n[PANIC] CPU Exception ");
  serial_print_dec(regs->int_no);
  serial_print(": ");
  serial_print(exception_messages[regs->int_no]);
  serial_print(from_user ? " (in Ring 3)\n" : " (in the kernel)\n");
  serial_print(" ");
  isr_print_reg("EAX", regs->eax);
  isr_print_reg("EBX", regs->ebx);
  isr_print_reg("ECX", regs->ecx);
  isr_print_reg("EDX", regs->edx);
  serial_print("\n ");
  isr_print_reg("ESI", regs->esi);
  isr_print_reg("EDI", regs->edi);
  isr_print_reg("EBP", regs->ebp);
  isr_print_reg("ESP", esp);
  serial_print("\n ");
  isr_print_reg("EIP", regs->eip);
  isr_print_reg("CS", regs->cs);
  isr_print_reg("EFLAGS", regs->eflags);
  isr_print_reg("DS", regs->ds);
  isr_print_reg("ERR", regs->err_code);
  if (from_user) {
    isr_print_reg("SS", regs->ss);
  }
  serial_print("\n ");
  isr_print_reg("CR0", isr_read_cr0());
  isr_print_reg("CR2", cr2);
  isr_print_reg("CR3", isr_read_cr3());
  isr_print_reg("CR4", isr_read_cr4());
  serial_print("\n  running: ");
  if (pid < 0) {
    serial_print("kernel task");
  } else {
    serial_print("PID ");
    serial_print_dec((uint32_t)pid);
  }
  serial_print("\nSystem Halted.\n");
  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

/*
 * Double fault: runs as its own hardware task (IDT vector 8 is a task gate,
 * see kernel_main) on its own stack, so it also works when a kernel stack
 * ran into its guard page and the CPU could not push the page fault frame.
 * The task switch saved the state of the failing code in the kernel TSS.
 */
void isr_double_fault_task(void) {
  const tss32_t *tss = gdt_kernel_tss();
  uint32_t cr2 = isr_read_cr2();

  serial_print("\n[PANIC] CPU Exception 8: Double Fault");
  if (kstack_is_guard(cr2) || kstack_is_guard(tss->esp)) {
    serial_print(" - kernel stack overflow (guard page)");
  }
  serial_print("\n ");
  isr_print_reg("EAX", tss->eax);
  isr_print_reg("EBX", tss->ebx);
  isr_print_reg("ECX", tss->ecx);
  isr_print_reg("EDX", tss->edx);
  serial_print("\n ");
  isr_print_reg("ESI", tss->esi);
  isr_print_reg("EDI", tss->edi);
  isr_print_reg("EBP", tss->ebp);
  isr_print_reg("ESP", tss->esp);
  serial_print("\n ");
  isr_print_reg("EIP", tss->eip);
  isr_print_reg("CS", tss->cs);
  isr_print_reg("EFLAGS", tss->eflags);
  isr_print_reg("DS", tss->ds);
  serial_print("\n ");
  isr_print_reg("CR0", isr_read_cr0());
  isr_print_reg("CR2", cr2);
  isr_print_reg("CR3", isr_read_cr3());
  isr_print_reg("CR4", isr_read_cr4());
  serial_print("\nSystem Halted.\n");
  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

uint32_t isr_handler(registers_t *regs) {
  int from_user = ((regs->cs & 0x3U) == 0x3U);

  if (regs->int_no < 32) {
    /* read CR2 first: a later page fault would overwrite it */
    uint32_t fault_cr2 = regs->int_no == 14 ? isr_read_cr2() : 0;

    if (from_user && isr_is_process_fault(regs->int_no)) {
      serial_print("[USERFAULT] pid=");
      serial_print_dec((uint32_t)scheduler_get_current_pid());
      serial_print(" vec=");
      serial_print_dec(regs->int_no);
      serial_print(" (");
      serial_print(exception_messages[regs->int_no]);
      serial_print(") eip=0x");
      serial_print_hex(regs->eip);
      serial_print(" err=0x");
      serial_print_hex(regs->err_code);
      if (regs->int_no == 14) {
        serial_print(" cr2=0x");
        serial_print_hex(fault_cr2);
      }
      serial_print("\n");

      scheduler_mark_current_fault(regs->int_no, regs->err_code, fault_cr2);
      return scheduler_interrupt_exit((uint32_t)(uintptr_t)regs);
    }

    /* int3 in the kernel is a debugging aid: note it and go on */
    if (regs->int_no == 3 && !from_user) {
      serial_print("[ISR] Breakpoint in the kernel at 0x");
      serial_print_hex(regs->eip);
      serial_print(" (continuing)\n");
      return 0;
    }

    isr_panic(regs, fault_cr2);
  }

  /* Handle IRQ EOI */
  if (regs->int_no >= PIC1_OFFSET && regs->int_no <= PIC2_OFFSET + 7) {
    pic_send_eoi(regs->int_no - PIC1_OFFSET);
  }

  if (interrupt_handlers[regs->int_no] != 0) {
    isr_t handler = interrupt_handlers[regs->int_no];
    handler(regs);
  } else if (regs->int_no < PIC1_OFFSET || regs->int_no > PIC2_OFFSET + 7) {
    serial_print("[ISR] Unhandled vector ");
    serial_print_dec(regs->int_no);
    serial_print("\n");
  }

  return scheduler_interrupt_exit((uint32_t)(uintptr_t)regs);
}
