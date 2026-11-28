#![no_std]

use core::ffi::{CStr, c_char};
use core::panic::PanicInfo;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Error(pub i32);

pub type Result<T> = core::result::Result<T, Error>;

// Values and layout shared with include/gemos/{console,syscall}_abi.h.
const INVALID: Error = Error(-1);
const EVENT_KEY: u32 = 1;
const EVENT_CLOSE_REQUEST: u32 = 2;
pub const WAIT_FOREVER: u32 = u32::MAX;

#[repr(C)]
#[derive(Default)]
struct RawEvent {
    kind: u32,
    key_code: u32,
    character: u32,
    modifiers: u32,
}

unsafe extern "C" {
    static gemos_rust_key_escape: u32;
    fn gemos_rust_console_open(title: *const c_char, cols: u32, rows: u32) -> i32;
    fn gemos_rust_console_write(handle: i32, bytes: *const u8, length: usize) -> i32;
    fn gemos_rust_console_wait_event(handle: i32, event: *mut RawEvent, timeout_ms: u32) -> i32;
    fn gemos_rust_file_read(path: *const c_char, buffer: *mut u8, capacity: usize) -> i32;
    fn gemos_rust_file_write(path: *const c_char, bytes: *const u8, length: usize) -> i32;
    fn gemos_rust_debug_write(bytes: *const u8, length: usize) -> i32;
    fn gemos_rust_exit(status: i32) -> !;
}

fn syscall_result(value: i32) -> Result<i32> {
    if value < 0 {
        Err(Error(value))
    } else {
        Ok(value)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Event {
    Key {
        scan_code: u32,
        character: u32,
        modifiers: u32,
    },
    CloseRequested,
}

impl Event {
    pub fn is_escape(&self) -> bool {
        // GemOS maps the hardware scan code to a key in the character field.
        matches!(self, Self::Key { character, .. } if *character == unsafe { gemos_rust_key_escape })
    }
}

/// A hosted console. The kernel releases its window when the process exits.
pub struct Console(i32);

impl Console {
    pub fn open(title: &CStr, cols: u32, rows: u32) -> Result<Self> {
        // The syscall copies the NUL-terminated title before returning.
        syscall_result(unsafe { gemos_rust_console_open(title.as_ptr(), cols, rows) }).map(Self)
    }

    pub fn write(&self, bytes: &[u8]) -> Result<()> {
        // The kernel reads only the bytes covered by the slice.
        syscall_result(unsafe { gemos_rust_console_write(self.0, bytes.as_ptr(), bytes.len()) })?;
        Ok(())
    }

    /// Wait for input or a close request. A timeout returns `None`.
    pub fn wait_event(&self, timeout_ms: u32) -> Result<Option<Event>> {
        let mut event = RawEvent::default();
        // The kernel writes at most one event to this live, writable object.
        let ready = syscall_result(unsafe {
            gemos_rust_console_wait_event(self.0, &mut event, timeout_ms)
        })?;
        match (ready, event.kind) {
            (0, _) => Ok(None),
            (1, EVENT_KEY) => Ok(Some(Event::Key {
                scan_code: event.key_code,
                character: event.character,
                modifiers: event.modifiers,
            })),
            (1, EVENT_CLOSE_REQUEST) => Ok(Some(Event::CloseRequested)),
            _ => Err(INVALID),
        }
    }
}

/// Read a whole file. The buffer needs one extra byte for the kernel's NUL.
pub fn read_file<'a>(path: &CStr, buffer: &'a mut [u8]) -> Result<&'a [u8]> {
    // The path is NUL-terminated and the kernel can write up to buffer.len().
    let count = syscall_result(unsafe {
        gemos_rust_file_read(path.as_ptr(), buffer.as_mut_ptr(), buffer.len())
    })? as usize;
    buffer.get(..count).ok_or(INVALID)
}

pub fn write_file(path: &CStr, bytes: &[u8]) -> Result<usize> {
    // Both slices live through the syscall, including when GemFS yields.
    syscall_result(unsafe {
        gemos_rust_file_write(path.as_ptr(), bytes.as_ptr(), bytes.len())
    })
    .map(|count| count as usize)
}

pub fn debug_write(message: &str) {
    // The kernel copies the message into its own buffer.
    let _ = unsafe { gemos_rust_debug_write(message.as_ptr(), message.len()) };
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    debug_write("[RUST] application panicked\n");
    // GemOS terminates and reaps the current process on SYS_exit.
    unsafe { gemos_rust_exit(101) }
}
