#![no_std]

mod view;

use core::ffi::{CStr, c_char};
use gemos::fs::{self, DirectoryEntry, DirectoryPage, NAME_BYTES, PAGE_ENTRIES, PATH_BYTES};
use gemos::window::{Event, Frame, Icon, Key, MenuItem, Pointer, Window};
use gemos::{Error, Result, debug_write};
use view::Layout;

const FIRST_WIDTH: i32 = 518;
const FIRST_HEIGHT: i32 = 372;
const PAGE_STEP: u32 = 16;
const OPEN: u32 = 1;
const RENAME: u32 = 2;
const NEW_FOLDER: u32 = 3;

#[derive(Clone, Copy)]
struct Path {
    bytes: [u8; PATH_BYTES],
    len: usize,
}

impl Path {
    const ROOT: Self = {
        let mut bytes = [0; PATH_BYTES];
        bytes[0] = b'/';
        Self { bytes, len: 1 }
    };

    fn home() -> Self {
        let mut path = Self::ROOT;
        let home = fs::home().to_bytes_with_nul();
        path.bytes[..home.len()].copy_from_slice(home);
        path.len = home.len() - 1;
        path
    }

    fn cstr(&self) -> &CStr {
        CStr::from_bytes_with_nul(&self.bytes[..=self.len]).expect("terminated GemFS path")
    }

    fn child(self, name: &[u8]) -> Option<Self> {
        let mut child = self;
        let start = self.len + usize::from(self.len > 1);
        let end = start + name.len();
        if end >= PATH_BYTES {
            return None;
        }
        if self.len > 1 {
            child.bytes[self.len] = b'/';
        }
        child.bytes[start..end].copy_from_slice(name);
        child.bytes[end] = 0;
        child.len = end;
        Some(child)
    }

    fn parent(mut self) -> Self {
        let slash = self.bytes[..self.len].iter().rposition(|&byte| byte == b'/').unwrap_or(0);
        self.len = slash.max(1);
        self.bytes[self.len] = 0;
        self
    }

    fn log_opened(&self) {
        let prefix = b"[EXPLORER] Opened ";
        let mut line = [0; PATH_BYTES + 24];
        line[..prefix.len()].copy_from_slice(prefix);
        let end = prefix.len() + self.len;
        line[prefix.len()..end].copy_from_slice(&self.bytes[..self.len]);
        line[end] = b'\n';
        if let Ok(text) = core::str::from_utf8(&line[..=end]) {
            debug_write(text);
        }
    }
}

#[derive(Clone, Copy)]
enum NameAction {
    Create,
    Rename(Path),
}

struct NamePrompt {
    action: NameAction,
    name: [u8; NAME_BYTES + 1],
    len: usize,
    caret: usize,
    selected: bool,
}

impl NamePrompt {
    fn new(action: NameAction, initial: &[u8]) -> Self {
        let mut name = [0; NAME_BYTES + 1];
        name[..initial.len()].copy_from_slice(initial);
        Self {
            action, name, len: initial.len(), caret: 0,
            selected: !initial.is_empty(),
        }
    }

    fn bytes(&self) -> &[u8] {
        &self.name[..self.len]
    }

    fn valid(&self) -> bool {
        let bytes = self.bytes();
        !bytes.is_empty() && bytes.len() < NAME_BYTES && bytes != b"." &&
            bytes != b".." && !bytes.contains(&b'/')
    }

    fn edit(&mut self, key: Key) -> bool {
        match key {
            Key::Home => self.caret = 0,
            Key::End => self.caret = self.len,
            Key::Left => self.caret = self.caret.saturating_sub(1),
            Key::Right if self.selected => self.caret = self.len,
            Key::Right => self.caret = (self.caret + 1).min(self.len),
            Key::Byte(0x20..=0x7e) | Key::Backspace | Key::Delete => {
                if self.selected {
                    self.len = 0;
                    self.caret = 0;
                }
                match key {
                    Key::Byte(byte) if self.len < NAME_BYTES => {
                        self.name.copy_within(self.caret..self.len, self.caret + 1);
                        self.name[self.caret] = byte;
                        self.caret += 1;
                        self.len += 1;
                    }
                    Key::Backspace if self.caret > 0 => {
                        self.name.copy_within(self.caret..self.len, self.caret - 1);
                        self.caret -= 1;
                        self.len -= 1;
                    }
                    Key::Delete if self.caret < self.len => {
                        self.name.copy_within(self.caret + 1..self.len, self.caret);
                        self.len -= 1;
                    }
                    _ => {}
                }
            }
            _ => return false,
        }
        self.name[self.len] = 0;
        self.selected = false;
        true
    }
}

#[derive(Clone, Copy)]
enum Kind {
    Directory,
    Text,
    Program,
    File,
}

impl Kind {
    fn of(entry: &DirectoryEntry) -> Self {
        let name = entry.name();
        if entry.is_directory() {
            Self::Directory
        } else if suffix(name, b".txt") || suffix(name, b".gemtext") {
            Self::Text
        } else if suffix(name, b".elf") {
            Self::Program
        } else {
            Self::File
        }
    }
}

fn suffix(name: &[u8], extension: &[u8]) -> bool {
    name.len() >= extension.len()
        && name[name.len() - extension.len()..].eq_ignore_ascii_case(extension)
}

struct Explorer {
    path: Path,
    page: DirectoryPage,
    base: u32,
    first: u32,
    selected: Option<u32>,
    last_click: Option<Pointer>,
    drag: Option<i32>,
    loaded: bool,
    generation: u32,
    width: i32,
    height: i32,
    prompt: Option<NamePrompt>,
    status: [u8; 96],
    status_len: usize,
    error: bool,
    dirty: bool,
    frame: Frame,
}

impl Explorer {
    const fn new() -> Self {
        Self {
            path: Path::ROOT,
            page: DirectoryPage::new(),
            base: 0,
            first: 0,
            selected: None,
            last_click: None,
            drag: None,
            loaded: false,
            generation: 0,
            width: FIRST_WIDTH,
            height: FIRST_HEIGHT,
            prompt: None,
            status: [0; 96],
            status_len: 0,
            error: false,
            dirty: true,
            frame: Frame::new(),
        }
    }

    fn layout(&self) -> Layout {
        Layout::new(self.width, self.height, self.total())
    }

    fn total(&self) -> u32 {
        if self.loaded { self.page.total } else { 0 }
    }

    fn entry(&self, index: u32) -> Option<&DirectoryEntry> {
        if !self.loaded {
            return None;
        }
        let slot = index.checked_sub(self.base)? as usize;
        self.page.entries().get(slot)
    }

    fn set_status(&mut self, prefix: &[u8], detail: &[u8], error: bool) {
        let prefix_len = prefix.len().min(self.status.len());
        let detail_len = detail.len().min(self.status.len() - prefix_len);
        self.status[..prefix_len].copy_from_slice(&prefix[..prefix_len]);
        self.status[prefix_len..prefix_len + detail_len].copy_from_slice(&detail[..detail_len]);
        self.status_len = prefix_len + detail_len;
        self.error = error;
        self.dirty = true;
    }

    fn page_start(&self) -> u32 {
        let chunk = self.first - self.first % PAGE_STEP;
        if self.first - chunk + self.layout().rows > PAGE_ENTRIES as u32 {
            self.first
        } else {
            chunk
        }
    }

    fn load(&mut self, window: &Window) -> Result<()> {
        let selected_inode = self.selected.and_then(|index| self.entry(index)).map(|entry| entry.inode);
        let base = self.page_start();
        fs::read_directory(self.path.cstr(), base, &mut self.page)?;
        self.base = base;
        if self.loaded && self.generation != self.page.generation {
            self.last_click = None;
            let _ = window.menu(0, 0, &[]);
            if self.selected.and_then(|index| self.entry(index)).map(|entry| entry.inode)
                != selected_inode
            {
                self.selected = None;
            }
        }
        self.loaded = true;
        self.generation = self.page.generation;
        self.first = self.first.min(self.layout().last_top);
        if self.first < self.base {
            self.base = self.page_start();
            fs::read_directory(self.path.cstr(), self.base, &mut self.page)?;
            self.generation = self.page.generation;
        }
        self.dirty = true;
        Ok(())
    }

    fn navigate(&mut self, path: Path, window: &Window) {
        self.path = path;
        self.first = 0;
        self.selected = None;
        self.last_click = None;
        self.drag = None;
        self.prompt = None;
        self.status_len = 0;
        self.loaded = false;
        let _ = window.menu(0, 0, &[]);
        match self.load(window) {
            Ok(()) => self.path.log_opened(),
            Err(err) if self.path.len > 1 && err.0 != fs::NO_DISK => {
                self.path = Path::ROOT;
                if self.load(window).is_ok() {
                    self.path.log_opened();
                }
                self.set_status(b"", b"Folder unavailable. Showing /", true);
            }
            Err(err) => {
                self.generation = fs::generation();
                self.set_status(b"", error_text(err), true);
            }
        }
    }

    fn refresh(&mut self, window: &Window) {
        if self.load(window).is_err() {
            self.navigate(self.path, window);
        }
    }

    fn scroll(&mut self, top: i32, window: &Window) {
        let layout = self.layout();
        let first = top.clamp(0, layout.last_top as i32) as u32;
        if first == self.first {
            return;
        }
        self.first = first;
        self.last_click = None;
        let end = (first + layout.rows).min(self.total());
        if first < self.base || end > self.base + self.page.entries().len() as u32 {
            self.refresh(window);
        }
        self.dirty = true;
    }

    fn home_entry(&self, entry: &DirectoryEntry) -> bool {
        self.path.len == 1 && entry.is_directory() && entry.name() == &fs::home().to_bytes()[1..]
    }

    fn begin_new(&mut self) {
        if self.loaded {
            self.prompt = Some(NamePrompt::new(NameAction::Create, b""));
            self.status_len = 0;
            self.dirty = true;
        }
    }

    fn begin_rename(&mut self) {
        let Some(entry) = self.selected.and_then(|index| self.entry(index)).copied() else {
            return;
        };
        if self.home_entry(&entry) {
            self.set_status(b"", b"Home keeps its name", true);
        } else if let Some(path) = self.path.child(entry.name()) {
            self.prompt = Some(NamePrompt::new(NameAction::Rename(path), entry.name()));
            self.status_len = 0;
            self.dirty = true;
        } else {
            self.set_status(b"Path too long: ", entry.name(), true);
        }
    }

    fn submit_name(&mut self, window: &Window) {
        let Some(prompt) = &self.prompt else { return; };
        if !prompt.valid() {
            let message: &[u8] = if prompt.len >= NAME_BYTES {
                b"Name is longer than 55 characters"
            } else {
                b"Enter a valid name"
            };
            self.set_status(b"", message, true);
            return;
        }
        let result = match prompt.action {
            NameAction::Create => match self.path.child(prompt.bytes()) {
                Some(path) => fs::create_directory(path.cstr()),
                None => {
                    self.set_status(b"", b"Path too long", true);
                    return;
                }
            },
            NameAction::Rename(path) => {
                let name = CStr::from_bytes_with_nul(&prompt.name[..=prompt.len])
                    .expect("terminated GemFS name");
                fs::rename(path.cstr(), name)
            }
        };
        match result {
            Ok(()) => {
                let prompt = self.prompt.take().expect("active name prompt");
                let prefix: &[u8] = match prompt.action {
                    NameAction::Create => b"Created ",
                    NameAction::Rename(_) => b"Renamed to ",
                };
                self.selected = None;
                self.set_status(prefix, prompt.bytes(), false);
                self.refresh(window);
            }
            Err(err) => self.set_status(b"", error_text(err), true),
        }
    }

    fn cancel_name(&mut self) {
        self.prompt = None;
        self.status_len = 0;
        self.dirty = true;
    }

    fn open_selected(&mut self, window: &Window) {
        let Some(entry) = self.selected.and_then(|index| self.entry(index)).copied() else {
            return;
        };
        let Some(path) = self.path.child(entry.name()) else {
            self.set_status(b"Path too long: ", entry.name(), true);
            return;
        };
        self.last_click = None;
        match Kind::of(&entry) {
            Kind::Directory => self.navigate(path, window),
            Kind::Text | Kind::Program => {
                self.selected = None;
                if fs::open(path.cstr()).is_err() {
                    self.set_status(b"Could not start ", entry.name(), true);
                }
            }
            Kind::File => self.set_status(b"No program opens ", entry.name(), true),
        }
        self.dirty = true;
    }

    fn right_press(&mut self, point: Pointer, window: &Window) {
        self.last_click = None;
        let layout = self.layout();
        if !self.loaded || self.prompt.is_some() || !layout.content.contains(point.x, point.y) {
            return;
        }
        let row = layout.row_at(point.y);
        let index = self.first + row;
        self.status_len = 0;
        self.dirty = true;
        if row < layout.rows && let Some(entry) = self.entry(index).copied() {
            self.selected = Some(index);
            let open = MenuItem::new(OPEN, b"Open");
            let rename = MenuItem::new(RENAME, b"Rename");
            if self.home_entry(&entry) {
                let _ = window.menu(point.x, point.y, &[open]);
            } else if matches!(Kind::of(&entry), Kind::File) {
                let _ = window.menu(point.x, point.y, &[rename]);
            } else {
                let _ = window.menu(point.x, point.y, &[open, rename]);
            }
        } else if index >= self.total() || row >= layout.rows {
            self.selected = None;
            let _ = window.menu(point.x, point.y, &[MenuItem::new(NEW_FOLDER, b"New Folder")]);
        }
    }

    fn left_press(&mut self, point: Pointer, window: &Window) {
        let layout = self.layout();
        let last = self.last_click.take();
        if let Some(prompt) = &mut self.prompt {
            if layout.accept.contains(point.x, point.y) {
                self.submit_name(window);
            } else if layout.cancel.contains(point.x, point.y) {
                self.cancel_name();
            } else if layout.field.contains(point.x, point.y) {
                let start = view::visible_name(prompt, layout.field.w);
                let x = point.x - layout.field.x - 6;
                let mut caret = start;
                while caret < prompt.len &&
                    gemos::window::text_width(&prompt.name[start..caret + 1], view::FONT) <= x
                {
                    caret += 1;
                }
                prompt.caret = caret;
                prompt.selected = false;
                self.dirty = true;
            }
            return;
        }
        self.status_len = 0;
        self.dirty = true;
        if layout.up.contains(point.x, point.y) {
            if self.path.len > 1 {
                self.navigate(self.path.parent(), window);
            }
        } else if layout.last_top > 0 && layout.track.contains(point.x, point.y) {
            let thumb = layout.thumb(self.first, self.total());
            if thumb.contains(point.x, point.y) {
                self.drag = Some(point.y - thumb.y);
            } else {
                let page = layout.rows as i32;
                let delta = if point.y < thumb.y { -page } else { page };
                self.scroll(self.first as i32 + delta, window);
            }
        } else if layout.list.contains(point.x, point.y) {
            let index = self.first + layout.row_at(point.y);
            if self.entry(index).is_some() {
                let twice = last.is_some_and(|last| {
                    point.time_ms.wrapping_sub(last.time_ms) <= 500
                        && (last.x - point.x).abs() <= 5
                        && (last.y - point.y).abs() <= 5
                });
                if self.selected == Some(index) && twice {
                    self.open_selected(window);
                } else {
                    self.selected = Some(index);
                    self.last_click = Some(point);
                }
            } else if index >= self.total() {
                self.selected = None;
            }
        }
    }

    fn drag_to(&mut self, point: Pointer, window: &Window) {
        let Some(offset) = self.drag else { return; };
        let layout = self.layout();
        if layout.last_top == 0 {
            return;
        }
        let thumb = layout.thumb(self.first, self.total());
        let span = layout.track.h - thumb.h;
        if span > 0 {
            let delta = (point.y - layout.track.y - offset).clamp(0, span);
            self.scroll((delta * layout.last_top as i32 + span / 2) / span, window);
        }
    }

    fn key(&mut self, key: Key, control: bool, alt: bool, window: &Window) {
        self.last_click = None;
        if let Some(prompt) = &mut self.prompt {
            match key {
                Key::Escape => self.cancel_name(),
                Key::Enter => self.submit_name(window),
                Key::Byte(b'a' | b'A') if control => {
                    prompt.caret = 0;
                    prompt.selected = true;
                    self.dirty = true;
                }
                _ if !control && !alt && prompt.edit(key) => {
                    self.status_len = 0;
                    self.dirty = true;
                }
                _ => {}
            }
            return;
        }
        let layout = self.layout();
        let top = self.first as i32;
        let page = layout.rows as i32;
        match key {
            Key::Up => self.scroll(top - 1, window),
            Key::Down => self.scroll(top + 1, window),
            Key::PageUp => self.scroll(top - page, window),
            Key::PageDown => self.scroll(top + page, window),
            Key::Home => self.scroll(0, window),
            Key::End => self.scroll(layout.last_top as i32, window),
            Key::Enter => self.open_selected(window),
            Key::F2 => self.begin_rename(),
            Key::Backspace if self.path.len > 1 => self.navigate(self.path.parent(), window),
            _ => {}
        }
    }

    fn event(&mut self, event: Event, window: &Window) {
        match event {
            Event::Key { key, control, alt } => self.key(key, control, alt, window),
            Event::LeftPress(point) => self.left_press(point, window),
            Event::RightPress(point) => self.right_press(point, window),
            Event::PointerMove(point) => self.drag_to(point, window),
            Event::LeftRelease => self.drag = None,
            Event::Resize { width, height } => {
                self.width = width;
                self.height = height;
                self.first = self.first.min(self.layout().last_top);
                if self.loaded { self.refresh(window); }
                self.dirty = true;
            }
            Event::Menu(OPEN) => self.open_selected(window),
            Event::Menu(RENAME) => self.begin_rename(),
            Event::Menu(NEW_FOLDER) => self.begin_new(),
            #[cfg(gemos_selftest)]
            Event::Menu(u32::MAX) => panic!("Explorer self-test panic"),
            _ => {}
        }
    }
}

fn error_text(error: Error) -> &'static [u8] {
    match error.0 {
        fs::NO_DISK => b"No GemFS data disk",
        fs::NAME_IN_USE => b"That name is already taken",
        -4 => b"GemFS has no room",
        -6 => b"The entry is missing",
        -7 => b"The entry is read-only",
        _ => b"Could not complete the operation",
    }
}

// The retained drawing list and directory page live outside the 8 KiB stack.
static mut EXPLORER: Explorer = Explorer::new();

fn run() -> Result<()> {
    let window = Window::open(c"File Explorer", FIRST_WIDTH as u32, FIRST_HEIGHT as u32, Icon::Folder)?;
    // GemOS starts one thread in the process. Only this event loop uses the state.
    let explorer = unsafe { &mut *(&raw mut EXPLORER) };
    explorer.navigate(Path::home(), &window);
    loop {
        if explorer.dirty {
            view::render(explorer, &window)?;
            explorer.dirty = false;
        }
        let event = window.wait_event(180)?;
        if matches!(event, Some(Event::CloseRequested)) {
            return Ok(());
        }
        if explorer.generation != fs::generation() {
            explorer.refresh(&window);
        }
        if let Some(event) = event {
            explorer.event(event, &window);
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn main(_argc: i32, _argv: *const *const c_char) -> i32 {
    match run() {
        Ok(()) => 0,
        Err(_) => {
            debug_write("[EXPLORER] Could not run the window\n");
            1
        }
    }
}
