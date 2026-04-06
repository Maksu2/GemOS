#include "keyboard.h"
#include "../include/io.h"
#include "../kernel/include/event.h"
#include "../kernel/isr.h"
#include "pic.h"
#include "serial.h"

#define KBD_DATA_PORT 0x60

/* Scancodes for modifiers */
#define SC_LSHIFT_PRESS 0x2A
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_PRESS 0x36
#define SC_RSHIFT_RELEASE 0xB6
#define SC_CAPSLOCK 0x3A

/* Modifier state (0 = off, 1 = on) */
static int shift_pressed = 0;
static int caps_lock = 0;

/* Lowercase scancode map (Scancode Set 1) */
static char scancode_lower[128] = {
    0,         KEY_ESC, '1',  '2', '3',       '4', '5',           '6',
    '7',       '8',     '9',  '0', '-',       '=', KEY_BACKSPACE, '\t',
    'q',       'w',     'e',  'r', 't',       'y', 'u',           'i',
    'o',       'p',     '[',  ']', KEY_ENTER, 0, /* Ctrl */
    'a',       's',     'd',  'f', 'g',       'h', 'j',           'k',
    'l',       ';',     '\'', '`', 0, /* Left Shift */
    '\\',      'z',     'x',  'c', 'v',       'b', 'n',           'm',
    ',',       '.',     '/',  0, /* Right Shift */
    '*',       0,                /* Alt */
    KEY_SPACE, 0,                /* Caps Lock */
                                 /* F1-F10, etc. ignored */
};

/* Uppercase/shifted scancode map */
static char scancode_upper[128] = {
    0,         KEY_ESC, '!', '@', '#',       '$', '%',           '^',
    '&',       '*',     '(', ')', '_',       '+', KEY_BACKSPACE, '\t',
    'Q',       'W',     'E', 'R', 'T',       'Y', 'U',           'I',
    'O',       'P',     '{', '}', KEY_ENTER, 0, /* Ctrl */
    'A',       'S',     'D', 'F', 'G',       'H', 'J',           'K',
    'L',       ':',     '"', '~', 0, /* Left Shift */
    '|',       'Z',     'X', 'C', 'V',       'B', 'N',           'M',
    '<',       '>',     '?', 0, /* Right Shift */
    '*',       0,               /* Alt */
    KEY_SPACE, 0,               /* Caps Lock */
};

/* For visual testing */
static uint8_t last_key = 0;

void keyboard_callback(registers_t *regs) {
  (void)regs;

  uint8_t scancode = inb(KBD_DATA_PORT);

  /* Handle key release (bit 7 set) */
  if (scancode & 0x80) {
    uint8_t release_code = scancode & 0x7F;

    /* Track Shift release */
    if (release_code == (SC_LSHIFT_PRESS) ||
        release_code == (SC_RSHIFT_PRESS)) {
      shift_pressed = 0;
    }

    /* Push key release event */
    event_t ev;
    ev.type = EVENT_KEY_RELEASE;
    ev.data.key.key_code = release_code;
    ev.data.key.character = 0;
    event_push(ev);
  } else {
    /* Key press */
    last_key = scancode;

    /* Handle Shift press */
    if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
      shift_pressed = 1;
      return;
    }

    /* Handle CapsLock toggle */
    if (scancode == SC_CAPSLOCK) {
      caps_lock = !caps_lock;
      serial_print("[KBD] CapsLock: ");
      serial_print(caps_lock ? "ON\n" : "OFF\n");
      return;
    }

    /* Handle Extended Keys (E0 Prefix) */
    static int e0_prefix = 0;
    if (scancode == 0xE0) {
      e0_prefix = 1;
      return;
    }

    if (e0_prefix) {
      e0_prefix = 0;
      char key = 0;
      if (scancode == 0x48)
        key = KEY_UP;
      else if (scancode == 0x50)
        key = KEY_DOWN;
      else if (scancode == 0x4B)
        key = KEY_LEFT;
      else if (scancode == 0x4D)
        key = KEY_RIGHT;

      if (key != 0) {
        serial_print("[KEY] Extended: ");
        serial_print_hex(key);
        serial_print("\n");

        event_t ev;
        ev.type = EVENT_KEY_PRESS;
        ev.data.key.key_code = scancode; // Raw scancode
        ev.data.key.character = key;     // Mapped key
        event_push(ev);
      }
      return;
    }

    if (scancode < 128) {
      /* Determine effective case: shift XOR capslock */
      int use_upper = shift_pressed ^ caps_lock;

      /* For non-letter keys, only shift matters (not capslock) */
      char key_lower = scancode_lower[scancode];
      char key_upper = scancode_upper[scancode];

      /* CapsLock only affects letters (a-z) */
      int is_letter = (key_lower >= 'a' && key_lower <= 'z');

      char key;
      if (is_letter) {
        key = use_upper ? key_upper : key_lower;
      } else {
        /* Non-letters: only shift affects */
        key = shift_pressed ? key_upper : key_lower;
      }

      if (key != 0) {
        serial_print("[KEY] Down: ");
        serial_putc(key);
        serial_print("\n");

        event_t ev;
        ev.type = EVENT_KEY_PRESS;
        ev.data.key.key_code = scancode;
        ev.data.key.character = key;
        event_push(ev);
      }
    }
  }
}

uint8_t keyboard_get_last_key(void) {
  uint8_t k = last_key;
  last_key = 0;
  return k;
}

void init_keyboard(void) {
  register_interrupt_handler(33, keyboard_callback);
  pic_clear_mask(1);
  serial_print("[KBD] Driver initialized (IRQ1)\n");
}
