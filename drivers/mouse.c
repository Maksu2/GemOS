#include "mouse.h"
#include "../drivers/vbe.h" /* For screen clamping */
#include "../include/io.h"
#include "../kernel/include/event.h"
#include "../kernel/isr.h"
#include "../kernel/ui/cursor.h" /* Access global cursor state */
#include "../kernel/ui/ui_scale.h"
#include "pic.h"
#include "serial.h"

#define MOUSE_PORT_DATA 0x60
#define MOUSE_PORT_CMD 0x64
#define MOUSE_PORT_STATUS 0x64

static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];

/* Cursor Position handled via global 'cursor' struct now */
/* static int32_t mouse_x = 400;  REMOVED */
/* static int32_t mouse_y = 300;  REMOVED */

/* Helper: Wait for mouse ports to be ready */
void mouse_wait(uint8_t type) {
  uint32_t timeout = 100000;
  if (type == 0) {
    /* Wait for data to be available */
    while (timeout--) {
      if ((inb(MOUSE_PORT_STATUS) & 1) == 1)
        return;
    }
    return;
  } else {
    /* Wait for signal to send */
    while (timeout--) {
      if ((inb(MOUSE_PORT_STATUS) & 2) == 0)
        return;
    }
    return;
  }
}

/* Write byte to mouse */
void mouse_write(uint8_t byte) {
  /* Wait for ready to write command */
  mouse_wait(1);
  outb(MOUSE_PORT_CMD, 0xD4);
  /* Wait for ready to write data */
  mouse_wait(1);
  outb(MOUSE_PORT_DATA, byte);
}

uint8_t mouse_read(void) {
  mouse_wait(0);
  return inb(MOUSE_PORT_DATA);
}

void mouse_callback(registers_t *regs) {
  (void)regs;

  uint8_t status = inb(MOUSE_PORT_CMD);
  /* Check if buffer is full and comes from mouse (bit 5) */
  if (!(status & 0x20)) {
    return;
  }

  uint8_t data = inb(MOUSE_PORT_DATA);

  switch (mouse_cycle) {
  case 0:
    /* Byte 0: Status */
    /* Check Bit 3 (Always 1) - Sync Bit */
    if ((data & 0x08) == 0) {
      /* Lost sync, reset cycle */
      mouse_cycle = 0;
      return;
    }
    mouse_byte[0] = data;
    mouse_cycle++;
    break;
  case 1:
    mouse_byte[1] = data;
    mouse_cycle++;
    break;
  case 2:
    mouse_byte[2] = data;
    mouse_cycle = 0;

    /* Strict Check: Overflow */
    /* If X or Y Overflow bits are set (Bit 6, 7 of Byte 0), discard packet */
    if (mouse_byte[0] & 0xC0) {
      return;
    }

    /* Decode Packet using Signed 8-bit Deltas */
    /* We trust the 9-bit sign extension by checking byte 0, OR we just trust
     * int8_t cast. */
    /* Most robust: Use int8_t cast, but verify sign bits match if needed. */
    /* Standard PS/2 practice: int8_t cast is sufficient for standard 3-byte
     * packets. */
    int8_t dx = (int8_t)mouse_byte[1];
    int8_t dy = (int8_t)mouse_byte[2];

    /* Update absolute position (Negate DY because PS/2 Y is up-positive, Screen
     * Y+ is DOWN) */
    /* Update GLOBAL STATE directly IN LOGICAL UNITS */

    /* Robust Logic Clamp */
    /* Get Logical Screen Size */
    int logical_w = (int)(vbe_get_width() / ui_scale);
    int logical_h = (int)(vbe_get_height() / ui_scale);

    /* Update cursor */
    cursor.x += dx;
    cursor.y -= dy;

    /* Clamp to [0, logical_dim - 1] */
    if (cursor.x < 0)
      cursor.x = 0;
    if (cursor.y < 0)
      cursor.y = 0;
    if (cursor.x >= logical_w)
      cursor.x = logical_w - 1;
    if (cursor.y >= logical_h)
      cursor.y = logical_h - 1;

    /* Push Mouse Event (for clicks/updates signal) */
    event_t ev;
    ev.type = EVENT_MOUSE_MOVE;

    /* Check for clicks */
    uint8_t btn = mouse_byte[0] & 0x7;
    static uint8_t last_btn = 0;

    /* Detect Left Button Changes (Bit 0) */
    if ((btn & 0x01) != (last_btn & 0x01)) {
      if (btn & 0x01) {
        ev.type = EVENT_MOUSE_CLICK;
      } else {
        ev.type = EVENT_MOUSE_RELEASE;
      }
    }

    last_btn = btn;

    ev.data.mouse.x = cursor.x;
    ev.data.mouse.y = cursor.y;
    ev.data.mouse.dx = dx;
    ev.data.mouse.dy = dy;
    ev.data.mouse.buttons = btn;

    /* Optimization: Only push if something actually happened */
    if (ev.type != EVENT_MOUSE_MOVE || dx != 0 || dy != 0) {
      if (!event_push(ev)) {
        /* Queue full, drop event but cursor state IS updated */
      }
    }

    break;
  }
}

/* Getter for visual test - non-blocking */
static uint8_t mouse_buttons = 0;
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
  if (x)
    *x = cursor.x;
  if (y)
    *y = cursor.y;

  /* We need to extract buttons from the last packet */
  /* Flags byte (byte 0) contains buttons: Left=bit0, Right=bit1, Middle=bit2 */
  /* Since we don't store it globally yet, we update it from byte 0 */
  mouse_buttons = mouse_byte[0] & 0x07;

  if (buttons)
    *buttons = mouse_buttons;
}

void init_mouse(void) {
  uint8_t status;

  /* Enable Auxiliary Device */
  mouse_wait(1);
  outb(MOUSE_PORT_CMD, 0xA8);

  /* Enable Interrupts for Mouse */
  mouse_wait(1);
  outb(MOUSE_PORT_CMD, 0x20); // Get Compaq Status
  mouse_wait(0);
  status = inb(MOUSE_PORT_DATA) | 2; // Enable IRQ12
  mouse_wait(1);
  outb(MOUSE_PORT_CMD, 0x60); // Set Compaq Status
  mouse_wait(1);
  outb(MOUSE_PORT_DATA, status);

  /* Use default settings */
  mouse_write(0xF6);
  mouse_read(); /* Ack */

  /* Enable Packet Streaming */
  mouse_write(0xF4);
  mouse_read(); /* Ack */

  /* Register IRQ12 handler */
  // IRQ12 is mapped to ISR 44 (32 + 12)
  register_interrupt_handler(44, mouse_callback);

  /* Enable IRQ12 (Slave) and IRQ2 (Cascade) manually */
  pic_clear_mask(12);
  pic_clear_mask(2);

  serial_print("[MOUSE] Driver initialized (IRQ12)\n");
}
