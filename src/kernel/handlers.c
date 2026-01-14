#include "../drivers/io.h"
#include "../drivers/video.h"
#include "idt.h"
#include "window.h"

// Scancode Map (Unshifted)
// 0x00 - 0x39 ... 0xXX
char kbd_US[128] = {0,    27,  '1', '2',  '3',  '4',  '5', '6', '7',  '8',
                    '9',  '0', '-', '=',  '\b', '\t', 'q', 'w', 'e',  'r',
                    't',  'y', 'u', 'i',  'o',  'p',  '[', ']', '\n', 0,
                    'a',  's', 'd', 'f',  'g',  'h',  'j', 'k', 'l',  ';',
                    '\'', '`', 0,   '\\', 'z',  'x',  'c', 'v', 'b',  'n',
                    'm',  ',', '.', '/',  0,    '*',  0,   ' '};

void keyboard_handler() {
  uint8_t scancode = inb(0x60);
  static int shift_pressed = 0;

  if (scancode == 0x2A || scancode == 0x36) {
    shift_pressed = 1;
    return;
  } // Left/Right Shift Down
  if (scancode == 0xAA || scancode == 0xB6) {
    shift_pressed = 0;
    return;
  } // Left/Right Shift Up

  // Ignore break codes for other keys (scancode & 0x80)
  if (!(scancode & 0x80)) {
    char c = kbd_US[scancode];

    // Shift Logic (Simple Uppercase)
    if (shift_pressed) {
      if (c >= 'a' && c <= 'z')
        c -= 32;
      else {
        // Basic symbols map
        if (c == '1')
          c = '!';
        else if (c == '2')
          c = '@';
        else if (c == '3')
          c = '#';
        else if (c == '4')
          c = '$';
        else if (c == '5')
          c = '%';
        else if (c == '6')
          c = '^';
        else if (c == '7')
          c = '&';
        else if (c == '8')
          c = '*';
        else if (c == '9')
          c = '(';
        else if (c == '0')
          c = ')';
        else if (c == '-')
          c = '_';
        else if (c == '=')
          c = '+';
        else if (c == '[')
          c = '{';
        else if (c == ']')
          c = '}';
        else if (c == ';')
          c = ':';
        else if (c == '\'')
          c = '"';
        else if (c == ',')
          c = '<';
        else if (c == '.')
          c = '>';
        else if (c == '/')
          c = '?';
      }
    }

    if (c) {
      // Feed to focused window
      wm_handle_keyboard(c);
    }
  }
}

void mouse_wait(uint8_t a_type) {
  uint32_t _time_out = 100000;
  if (a_type == 0) {
    while (_time_out--) {
      if ((inb(0x64) & 1) == 1)
        return;
    }
    return;
  } else {
    while (_time_out--) {
      if ((inb(0x64) & 2) == 0)
        return;
    }
    return;
  }
}

void mouse_write(uint8_t a_write) {
  mouse_wait(1);
  outb(0x64, 0xD4);
  mouse_wait(1);
  outb(0x60, a_write);
}

uint8_t mouse_read() {
  mouse_wait(0);
  return inb(0x60);
}

void init_mouse() {
  uint8_t _status;

  // Enable Aux Device
  mouse_wait(1);
  outb(0x64, 0xA8);

  // Enable Interrupts
  mouse_wait(1);
  outb(0x64, 0x20);
  mouse_wait(0);
  _status = (inb(0x60) | 2);
  _status = _status | (1 << 1);
  // Compaq Status: Bit 1 = Enable IRQ 12?
  // Also force Bit 2=0 (disable system flag)?

  mouse_wait(1);
  outb(0x64, 0x60);
  mouse_wait(1);
  outb(0x60, _status);

  // Set Defaults
  mouse_write(0xF6);
  mouse_read();

  // Enable
  mouse_write(0xF4);
  mouse_read();
}

// Need to track global mouse position for WM
// window.c uses extern ints, we define them here or there.
// Defined in window.c usually or here?
// window.c had "extern int mouse_x", so let's define them somewhere real.
// I will define them in kernel.c or here. Let's define here if window.c has
// extern. Actually window.c has "extern int mouse_x" so it expects it
// elsewhere. I'll define them in handlers.c

int mouse_x = 400;
int mouse_y = 300;
int mouse_buttons = 0;

void mouse_handler() {
  uint8_t status = inb(0x64);
  if (!(status & 0x20) && (status & 1)) {
    // Not mouse?
  }

  uint8_t data = inb(0x60);
  static uint8_t mouse_cycle = 0;
  static uint8_t mouse_byte[3];

  // Synchronization: Bit 3 of Byte 0 must be 1.
  if (mouse_cycle == 0) {
    if ((data & 0x08) == 0) {
      // Not aligned at start of packet. Ignore / Desync logic.
      return;
    }
  }

  mouse_byte[mouse_cycle++] = data;

  if (mouse_cycle == 3) {
    mouse_cycle = 0;

    uint8_t flags = mouse_byte[0];
    uint8_t x_raw = mouse_byte[1];
    uint8_t y_raw = mouse_byte[2];

    // Check Overflows (Bit 6=X, Bit 7=Y)
    if ((flags & 0x80) || (flags & 0x40)) {
      // Transform is invalid if overflow occurred. Discard packet to prevent
      // erratic jumps.
      return;
    }

    // 9-bit Sign Extension logic
    // If Sign bit (Bit 4 for X, Bit 5 for Y) is set, we sign extend the 8-bit
    // raw value. x_raw is uint8_t 0..255. If negative, we OR with 0xFFFFFF00
    // (assuming 32-bit int).

    int x_mov = (int)x_raw;
    if (flags & 0x10) { // X Sign Bit
      x_mov |= 0xFFFFFF00;
    }

    int y_mov = (int)y_raw;
    if (flags & 0x20) { // Y Sign Bit
      y_mov |= 0xFFFFFF00;
    }

    // Apply movement Linear 1:1
    // Hardware Y+ is Up, Logic Y+ is Down -> Subtract Y
    mouse_x += x_mov;
    mouse_y -= y_mov;

    // Clamp to screen
    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_x > 1023)
      mouse_x = 1023; // Assumed Width
    if (mouse_y < 0)
      mouse_y = 0;
    if (mouse_y > 767)
      mouse_y = 767; // Assumed Height

    // Buttons
    mouse_buttons = flags & 0x07;

    wm_handle_mouse(mouse_x, mouse_y, mouse_buttons);
  }
}

void irq_handler(registers_t r) {
  if (r.int_no == 33) {
    keyboard_handler();
  }
  if (r.int_no == 44) {
    mouse_handler();
  }

  // EOI
  if (r.int_no >= 40) {
    outb(0xA0, 0x20); // Slave
  }
  outb(0x20, 0x20); // Master
}
