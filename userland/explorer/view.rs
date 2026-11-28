use gemos::fs::{DirectoryEntry, DirectoryPage};
use gemos::window::{Frame, Icon, Rect, Window, text_width};
use gemos::Result;

use super::{Confirm, Explorer, Kind, NameAction, NamePrompt, PATH_BYTES};

const TOOLBAR: i32 = 32;
const HEADER: i32 = 25;
const ROW: i32 = 24;
const FOOTER: i32 = 24;
const SCROLLBAR: i32 = 14;
const TYPE_WIDTH: i32 = 108;
pub(super) const FONT: u32 = 13;

const PAPER_SHADED: u32 = 0xe7e4dc;
const HEADER_PAPER: u32 = 0xedeae3;
const INK: u32 = 0x191a1b;
const MUTED: u32 = 0x595751;
const RULE: u32 = 0xbfbbb0;
const FRAME: u32 = 0x8d8981;
const ACCENT: u32 = 0xb45436;
const SELECTION: u32 = 0xeaded3;
const ERROR: u32 = 0x953f32;

pub(super) struct Layout {
    pub width: i32,
    pub rows: u32,
    pub last_top: u32,
    pub up: Rect,
    pub list: Rect,
    pub track: Rect,
    pub content: Rect,
    pub field: Rect,
    pub accept: Rect,
    pub cancel: Rect,
    pub delete: Rect,
    footer: Rect,
}

impl Layout {
    pub fn new(width: i32, height: i32, total: u32) -> Self {
        let available = (height - TOOLBAR - HEADER - FOOTER).max(0);
        let rows = (available / ROW) as u32;
        let last_top = total.saturating_sub(rows);
        let list_y = TOOLBAR + HEADER;
        let list_width = width - SCROLLBAR;
        let list_height = rows as i32 * ROW;
        let right = width - 8;
        Self {
            width, rows, last_top,
            up: Rect::new(8, 4, 38, 24),
            list: Rect::new(0, list_y, list_width, list_height),
            track: Rect::new(list_width, list_y, SCROLLBAR, list_height),
            content: Rect::new(0, list_y, if last_top > 0 { list_width } else { width }, available),
            field: Rect::new(96, 4, width - 218, 24),
            accept: Rect::new(right - 106, 4, 38, 24),
            cancel: Rect::new(right - 60, 4, 60, 24),
            delete: Rect::new(right - 128, 4, 60, 24),
            footer: Rect::new(0, height - FOOTER, width, FOOTER),
        }
    }

    pub fn row_at(&self, y: i32) -> u32 {
        ((y - self.list.y).max(0) / ROW) as u32
    }

    pub fn thumb(&self, first: u32, total: u32) -> Rect {
        let height = (self.track.h * self.rows as i32 / total as i32).max(16).min(self.track.h);
        let y = self.track.y +
            (self.track.h - height) * first.min(self.last_top) as i32 / self.last_top as i32;
        Rect::new(self.track.x + 3, y, self.track.w - 6, height)
    }
}

fn button(frame: &mut Frame, area: Rect, label: &[u8], color: u32) {
    frame.fill(Rect::new(area.x + 1, area.y + 1, area.w - 2, area.h - 2), HEADER_PAPER);
    frame.outline(area, RULE);
    frame.text(area.x + 8, area.y + 4, area.w - 12, FONT, color, label);
}

pub(super) fn visible_name(prompt: &NamePrompt, field_width: i32) -> usize {
    let mut start = 0;
    while start < prompt.caret && text_width(&prompt.name[start..prompt.caret], FONT) > field_width - 13 {
        start += 1;
    }
    start
}

fn name_toolbar(frame: &mut Frame, layout: &Layout, prompt: &NamePrompt) {
    let field = layout.field;
    let label: &[u8] = match prompt.action {
        NameAction::Create => b"New Folder",
        NameAction::CreateText => b"New File",
        NameAction::Rename(_) => b"Rename",
    };
    frame.text(8, field.y + 4, field.x - 15, FONT, INK, label);
    frame.outline(field, ACCENT);
    button(frame, layout.accept, b"OK", INK);
    button(frame, layout.cancel, b"Cancel", MUTED);

    let start = visible_name(prompt, field.w);
    let text_x = field.x + 6;
    let available = field.w - 12;
    if prompt.selected {
        frame.fill(Rect::new(text_x, field.y + 3, text_width(prompt.bytes(), FONT).min(available),
                             field.h - 6), SELECTION);
    }
    frame.text(text_x, field.y + 4, available, FONT, INK, &prompt.bytes()[start..]);
    if !prompt.selected {
        let caret = text_x + text_width(&prompt.name[start..prompt.caret], FONT);
        frame.fill(Rect::new(caret, field.y + 4, 1, field.h - 8), ACCENT);
    }
}

fn confirm_toolbar(frame: &mut Frame, layout: &Layout, confirm: &Confirm) {
    let question: &[u8] = if confirm.directory {
        b"Delete this folder and everything in it?"
    } else {
        b"Delete this file?"
    };
    let name = confirm.path.name();
    let question_width = text_width(question, FONT);
    let room = layout.delete.x - 16;
    frame.text(8, 8, room, FONT, ERROR, question);
    frame.text(8 + question_width + 6, 8, room - question_width - 6, FONT, INK, name);
    button(frame, layout.delete, b"Delete", ERROR);
    button(frame, layout.cancel, b"Cancel", MUTED);
}

fn draw_entry(frame: &mut Frame, row: Rect, type_x: i32, entry: &DirectoryEntry, selected: bool) {
    let (icon, label): (_, &[u8]) = match Kind::of(entry) {
        Kind::Directory => (Icon::Folder, b"Folder"),
        Kind::Text => (Icon::Text, b"Text file"),
        Kind::Program => (Icon::Program, b"Program"),
        Kind::File => (Icon::File, b"File"),
    };
    if selected {
        frame.fill(row, SELECTION);
        frame.fill(Rect::new(row.x, row.y, 2, row.h), ACCENT);
    }
    frame.fill(Rect::new(row.x + 10, row.y + row.h - 1, row.w - 20, 1), HEADER_PAPER);
    frame.icon(row.x + 11, row.y + 4, icon, INK);
    frame.text(row.x + 38, row.y + 4, type_x - row.x - 46, FONT, INK, entry.name());
    frame.text(type_x, row.y + 4, row.x + row.w - type_x - 8, FONT, MUTED, label);
}

fn entry_at(page: &DirectoryPage, base: u32, index: u32) -> Option<&DirectoryEntry> {
    page.entries().get(index.checked_sub(base)? as usize)
}

fn count_text(mut total: u32, out: &mut [u8; 20]) -> &[u8] {
    let suffix: &[u8] = if total == 1 { b" item" } else { b" items" };
    out[10..10 + suffix.len()].copy_from_slice(suffix);
    let mut first = 10;
    loop {
        first -= 1;
        out[first] = b'0' + (total % 10) as u8;
        total /= 10;
        if total == 0 { break; }
    }
    &out[first..10 + suffix.len()]
}

pub(super) fn render(explorer: &mut Explorer, window: &Window) -> Result<()> {
    let layout = explorer.layout();
    let total = explorer.total();
    let Explorer { frame, path, prompt, confirm, loaded, page, base, first, selected, status,
                   status_len, error, .. } = explorer;
    frame.clear();
    frame.fill(Rect::new(0, TOOLBAR - 1, layout.width, 1), RULE);

    if let Some(confirm) = confirm {
        confirm_toolbar(frame, &layout, confirm);
    } else if let Some(prompt) = prompt {
        name_toolbar(frame, &layout, prompt);
    } else {
        button(frame, layout.up, b"Up", if path.len > 1 { INK } else { MUTED });
        let bytes = &path.bytes[..path.len];
        let home = gemos::fs::home().to_bytes();
        let mut alias = [0; PATH_BYTES];
        let shown = if bytes.starts_with(home) &&
            (bytes.len() == home.len() || bytes.get(home.len()) == Some(&b'/'))
        {
            alias[0] = b'~';
            let tail = &bytes[home.len()..];
            alias[1..1 + tail.len()].copy_from_slice(tail);
            &alias[..1 + tail.len()]
        } else {
            bytes
        };
        frame.text(58, 7, layout.width - 66, 14, INK, shown);
    }

    let type_x = layout.list.w - TYPE_WIDTH;
    frame.fill(Rect::new(0, TOOLBAR, layout.width, HEADER), HEADER_PAPER);
    frame.text(10, TOOLBAR + 5, type_x - 16, FONT, MUTED, b"Name");
    frame.text(type_x, TOOLBAR + 5, TYPE_WIDTH - 8, FONT, MUTED, b"Type");
    frame.fill(Rect::new(0, layout.list.y - 1, layout.width, 1), RULE);
    if *loaded {
        for index in *first..(*first + layout.rows).min(total) {
            if let Some(entry) = entry_at(page, *base, index) {
                let y = layout.list.y + (index - *first) as i32 * ROW;
                draw_entry(frame, Rect::new(0, y, layout.list.w, ROW), type_x, entry,
                           *selected == Some(index));
            }
        }
    }
    if layout.last_top > 0 && layout.rows > 0 {
        frame.fill(layout.track, PAPER_SHADED);
        frame.fill(layout.thumb(*first, total), FRAME);
    }

    frame.fill(layout.footer, PAPER_SHADED);
    frame.fill(Rect::new(0, layout.footer.y, layout.width, 1), RULE);
    let mut count = [0; 20];
    frame.text(10, layout.footer.y + 4, 100, FONT, MUTED, count_text(total, &mut count));
    let current = if *loaded {
        selected.and_then(|index| entry_at(page, *base, index)).map(DirectoryEntry::name)
    } else {
        None
    };
    if *status_len > 0 && *error {
        frame.fill(Rect::new(0, layout.footer.y + 1, 2, FOOTER - 1), ERROR);
    }
    let (detail, color) = if *status_len > 0 {
        (&status[..*status_len], if *error { ERROR } else { INK })
    } else {
        (current.unwrap_or(b""), MUTED)
    };
    frame.text(116, layout.footer.y + 4, layout.width - 126, FONT, color, detail);
    window.present(frame)
}
