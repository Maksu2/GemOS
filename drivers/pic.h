#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

/* IRQ Base vectors */
#define PIC1_OFFSET 0x20
#define PIC2_OFFSET 0x28

void init_pic(void);
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);

#endif
