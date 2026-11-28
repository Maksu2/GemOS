#![no_std]

use core::ffi::c_char;
use gemos::{Console, Event, WAIT_FOREVER};

#[unsafe(no_mangle)]
pub extern "C" fn main(_argc: i32, _argv: *const *const c_char) -> i32 {
    match run() {
        Ok(()) => 0,
        Err(_) => {
            gemos::debug_write("[RUST] demo could not open its console or copy the example file\n");
            1
        }
    }
}

fn run() -> gemos::Result<()> {
    let console = Console::open(c"Rust on GemOS", 46, 9)?;
    let mut buffer = [0; 256];
    let example = gemos::read_file(c"/Rust example.txt", &mut buffer)?;
    gemos::write_file(c"/Rust copy.txt", example)?;
    console.write(example)?;
    console.write(b"\nSaved a copy as Rust copy.txt.\n\nEsc or X closes the app.\nP tests recovery from a Rust panic.")?;
    gemos::debug_write("[RUST] GemFS example copied\n");

    loop {
        match console.wait_event(WAIT_FOREVER)? {
            Some(Event::CloseRequested) => return Ok(()),
            Some(event) if event.is_escape() => return Ok(()),
            Some(Event::Key { character, .. })
                if character == u32::from(b'p') || character == u32::from(b'P') =>
            {
                panic!("demo panic requested");
            }
            _ => {}
        }
    }
}
