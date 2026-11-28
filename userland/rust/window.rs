use super::{Error, INVALID, RawEvent, Result, syscall_result};
use core::ffi::{CStr, c_char};

const DRAW_LIMIT: usize = 192;
const TEXT_BYTES: usize = 128;
const MENU_LIMIT: usize = 8;
const MENU_LABEL_BYTES: usize = 32;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Key {
    Byte(u8),
    Backspace,
    Enter,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    F2,
    Delete,
}

impl Key {
    fn from_character(character: u32) -> Self {
        const MAPPED: [Key; 13] = [
            Key::Backspace, Key::Enter, Key::Escape, Key::Up, Key::Down, Key::Left,
            Key::Right, Key::Home, Key::End, Key::PageUp, Key::PageDown, Key::F2,
            Key::Delete,
        ];
        // The immutable table is emitted from the GemOS C key ABI.
        let codes = unsafe { gemos_rust_window_keys };
        match codes.iter().position(|&code| u32::from(code) == character) {
            Some(index) => MAPPED[index],
            None => Key::Byte(character as u8),
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Pointer {
    pub x: i32,
    pub y: i32,
    pub time_ms: u32,
}

#[derive(Clone, Copy, Debug)]
pub enum Event {
    Key { key: Key, control: bool, alt: bool },
    CloseRequested,
    LeftPress(Pointer),
    RightPress(Pointer),
    PointerMove(Pointer),
    LeftRelease,
    Resize { width: i32, height: i32 },
    Menu(u32),
}

#[derive(Clone, Copy)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}

impl Rect {
    pub const fn new(x: i32, y: i32, w: i32, h: i32) -> Self {
        Self { x, y, w, h }
    }

    pub fn contains(self, x: i32, y: i32) -> bool {
        x >= self.x && x < self.x + self.w && y >= self.y && y < self.y + self.h
    }
}

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Icon {
    Folder = 1,
    Text = 2,
    Program = 3,
    File = 4,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct DrawCommand {
    kind: u32,
    area: RectAbi,
    color: u32,
    value: u32,
    text: [u8; TEXT_BYTES],
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RectAbi {
    x: i32,
    y: i32,
    w: i32,
    h: i32,
}

impl DrawCommand {
    const EMPTY: Self = Self {
        kind: 0,
        area: RectAbi { x: 0, y: 0, w: 0, h: 0 },
        color: 0,
        value: 0,
        text: [0; TEXT_BYTES],
    };
}

fn label<const N: usize>(bytes: &[u8]) -> [u8; N] {
    let mut out = [0; N];
    let count = bytes.len().min(N - 1);
    out[..count].copy_from_slice(&bytes[..count]);
    out
}

/// Retained drawing commands in client-relative logical pixels.
pub struct Frame {
    commands: [DrawCommand; DRAW_LIMIT],
    count: usize,
    overflowed: bool,
}

impl Frame {
    pub const fn new() -> Self {
        Self { commands: [DrawCommand::EMPTY; DRAW_LIMIT], count: 0, overflowed: false }
    }

    pub fn clear(&mut self) {
        self.count = 0;
        self.overflowed = false;
    }

    fn push(&mut self, kind: u32, rect: Rect, color: u32, value: u32, text: &[u8]) {
        let Some(command) = self.commands.get_mut(self.count) else {
            self.overflowed = true;
            return;
        };
        *command = DrawCommand {
            kind,
            area: RectAbi { x: rect.x, y: rect.y, w: rect.w, h: rect.h },
            color,
            value,
            text: label(text),
        };
        self.count += 1;
    }

    pub fn fill(&mut self, rect: Rect, color: u32) {
        if rect.w > 0 && rect.h > 0 {
            self.push(1, rect, color, 0, b"");
        }
    }

    pub fn outline(&mut self, rect: Rect, color: u32) {
        if rect.w > 0 && rect.h > 0 {
            self.push(2, rect, color, 0, b"");
        }
    }

    pub fn text(&mut self, x: i32, y: i32, width: i32, size: u32, color: u32, text: &[u8]) {
        if width > 0 && !text.is_empty() {
            self.push(3, Rect::new(x, y, width, 0), color, size, text);
        }
    }

    pub fn icon(&mut self, x: i32, y: i32, icon: Icon, color: u32) {
        self.push(4, Rect::new(x, y, 0, 0), color, icon as u32, b"");
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct MenuItem {
    action: u32,
    label: [u8; MENU_LABEL_BYTES],
}

impl MenuItem {
    pub fn new(action: u32, text: &[u8]) -> Self {
        Self { action, label: label(text) }
    }
}

#[repr(C)]
struct Menu {
    x: i32,
    y: i32,
    count: u32,
    items: [MenuItem; MENU_LIMIT],
}

unsafe extern "C" {
    static gemos_rust_window_keys: [u8; 13];
    fn gemos_rust_window_open(title: *const c_char, width: u32, height: u32, icon: u32) -> i32;
    fn gemos_rust_window_present(handle: i32, commands: *const DrawCommand, count: u32) -> i32;
    fn gemos_rust_window_menu(handle: i32, menu: *const Menu) -> i32;
    fn gemos_rust_text_width(text: *const c_char, size: u32) -> i32;
}

/// The kernel owns decorations, focus and destruction of a hosted window.
pub struct Window(i32);

impl Window {
    pub fn open(title: &CStr, width: u32, height: u32, icon: Icon) -> Result<Self> {
        syscall_result(unsafe { gemos_rust_window_open(title.as_ptr(), width, height, icon as u32) })
            .map(Self)
    }

    pub fn present(&self, frame: &Frame) -> Result<()> {
        if frame.overflowed {
            return Err(Error(-4));
        }
        syscall_result(unsafe {
            gemos_rust_window_present(self.0, frame.commands.as_ptr(), frame.count as u32)
        }).map(|_| ())
    }

    /// Open a system context menu. An empty slice dismisses this window's menu.
    pub fn menu(&self, x: i32, y: i32, items: &[MenuItem]) -> Result<()> {
        if items.len() > MENU_LIMIT {
            return Err(INVALID);
        }
        let mut menu = Menu {
            x, y, count: items.len() as u32,
            items: [MenuItem { action: 0, label: [0; MENU_LABEL_BYTES] }; MENU_LIMIT],
        };
        menu.items[..items.len()].copy_from_slice(items);
        syscall_result(unsafe { gemos_rust_window_menu(self.0, &menu) }).map(|_| ())
    }

    pub fn wait_event(&self, timeout_ms: u32) -> Result<Option<Event>> {
        let mut raw = RawEvent::default();
        let ready = syscall_result(unsafe {
            super::gemos_rust_console_wait_event(self.0, &mut raw, timeout_ms)
        })?;
        if ready == 0 {
            return Ok(None);
        }
        let pointer = Pointer {
            x: raw.key_code as i32,
            y: raw.character as i32,
            time_ms: raw.modifiers,
        };
        Ok(Some(match raw.kind {
            super::EVENT_KEY => Event::Key {
                key: Key::from_character(raw.character),
                control: raw.modifiers & 1 != 0,
                alt: raw.modifiers & 4 != 0,
            },
            super::EVENT_CLOSE_REQUEST => Event::CloseRequested,
            3 => Event::LeftPress(pointer),
            4 => Event::RightPress(pointer),
            5 => Event::PointerMove(pointer),
            6 => Event::LeftRelease,
            7 => Event::Resize { width: pointer.x, height: pointer.y },
            8 => Event::Menu(raw.key_code),
            _ => return Err(INVALID),
        }))
    }
}

pub fn text_width(text: &[u8], size: u32) -> i32 {
    let buffer = label::<TEXT_BYTES>(text);
    unsafe { gemos_rust_text_width(buffer.as_ptr().cast(), size) }.max(0)
}
