#include "pic.h"
#include "../include/io.h"

/* Helper to write to PIC ports with wait */
static void pic_send_cmd(uint16_t port, uint8_t cmd) {
  outb(port, cmd);
  io_wait();
}

static void pic_send_data(uint16_t port, uint8_t data) {
  outb(port, data);
  io_wait();
}

void init_pic(void) {
  /* ICW1: Init command, Cascade mode, ICW4 needed */
  pic_send_cmd(PIC1_COMMAND, 0x11);
  pic_send_cmd(PIC2_COMMAND, 0x11);

  /* ICW2: Vector offsets */
  pic_send_data(PIC1_DATA, PIC1_OFFSET); /* Master maps to 0x20-0x27 */
  pic_send_data(PIC2_DATA, PIC2_OFFSET); /* Slave maps to 0x28-0x2F */

  /* ICW3: Cascading setup */
  pic_send_data(PIC1_DATA, 4); /* Master has slave on IRQ2 */
  pic_send_data(PIC2_DATA, 2); /* Slave is connected to Master IRQ2 */

  /* ICW4: 8086 mode */
  pic_send_data(PIC1_DATA, 0x01);
  pic_send_data(PIC2_DATA, 0x01);

  /* OCW1: Mask all interrupts initially */
  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
  if (irq >= 8) {
    outb(PIC2_COMMAND, PIC_EOI);
  }
  outb(PIC1_COMMAND, PIC_EOI);
}

void pic_set_mask(uint8_t irq_line) {
  uint16_t port;
  uint8_t value;

  if (irq_line < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    irq_line -= 8;
  }

  value = inb(port) | (1 << irq_line);
  outb(port, value);
}

void pic_clear_mask(uint8_t irq_line) {
  uint16_t port;
  uint8_t value;

  if (irq_line < 8) {
    port = PIC1_DATA;
  } else {
    port = PIC2_DATA;
    irq_line -= 8;
  }

  value = inb(port) & ~(1 << irq_line);
  outb(port, value);
}
