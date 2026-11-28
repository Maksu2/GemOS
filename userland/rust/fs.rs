use super::{Result, syscall_result};
use core::ffi::{CStr, c_char};

pub const NAME_BYTES: usize = 56;
pub const PATH_BYTES: usize = 128;
pub const PAGE_ENTRIES: usize = 48;
pub const NAME_IN_USE: i32 = -8;
pub const NO_DISK: i32 = -9;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DirectoryEntry {
    pub inode: u32,
    kind: u32,
    name: [u8; NAME_BYTES],
}

impl DirectoryEntry {
    const EMPTY: Self = Self { inode: 0, kind: 0, name: [0; NAME_BYTES] };

    pub fn name(&self) -> &[u8] {
        let end = self.name.iter().position(|&byte| byte == 0).unwrap_or(NAME_BYTES);
        &self.name[..end]
    }

    pub fn is_directory(&self) -> bool {
        self.kind == 2
    }
}

#[repr(C)]
pub struct DirectoryPage {
    pub total: u32,
    pub generation: u32,
    count: u32,
    entries: [DirectoryEntry; PAGE_ENTRIES],
}

impl DirectoryPage {
    pub const fn new() -> Self {
        Self { total: 0, generation: 0, count: 0, entries: [DirectoryEntry::EMPTY; PAGE_ENTRIES] }
    }

    pub fn entries(&self) -> &[DirectoryEntry] {
        &self.entries[..(self.count as usize).min(PAGE_ENTRIES)]
    }
}

unsafe extern "C" {
    fn gemos_rust_directory_read(path: *const c_char, first: u32, page: *mut DirectoryPage) -> i32;
    fn gemos_rust_directory_create(path: *const c_char) -> i32;
    fn gemos_rust_entry_rename(path: *const c_char, name: *const c_char) -> i32;
    fn gemos_rust_open_path(path: *const c_char) -> i32;
    fn gemos_rust_entry_delete(path: *const c_char) -> i32;
    fn gemos_rust_entry_copy(source: *const c_char, target: *const c_char) -> i32;
    fn gemos_rust_fs_generation() -> u32;
    fn gemos_rust_home() -> *const c_char;
}

pub fn home() -> &'static CStr {
    // The ABI returns the address of the permanent, terminated GemOS home path.
    unsafe { CStr::from_ptr(gemos_rust_home()) }
}

pub fn read_directory(path: &CStr, first: u32, page: &mut DirectoryPage) -> Result<()> {
    syscall_result(unsafe { gemos_rust_directory_read(path.as_ptr(), first, page) }).map(|_| ())
}

pub fn create_directory(path: &CStr) -> Result<()> {
    syscall_result(unsafe { gemos_rust_directory_create(path.as_ptr()) }).map(|_| ())
}

pub fn rename(path: &CStr, name: &CStr) -> Result<()> {
    syscall_result(unsafe { gemos_rust_entry_rename(path.as_ptr(), name.as_ptr()) }).map(|_| ())
}

/// Delete a file, or a directory with everything in it.
pub fn delete(path: &CStr) -> Result<()> {
    syscall_result(unsafe { gemos_rust_entry_delete(path.as_ptr()) }).map(|_| ())
}

/// Copy a file, or a directory with everything in it, to `target`, which
/// must not exist (`NAME_IN_USE`). A failed copy leaves nothing behind.
pub fn copy(source: &CStr, target: &CStr) -> Result<()> {
    syscall_result(unsafe { gemos_rust_entry_copy(source.as_ptr(), target.as_ptr()) })
        .map(|_| ())
}

pub fn open(path: &CStr) -> Result<()> {
    syscall_result(unsafe { gemos_rust_open_path(path.as_ptr()) }).map(|_| ())
}

pub fn generation() -> u32 {
    unsafe { gemos_rust_fs_generation() }
}
