#!/usr/bin/env python3
"""GemOS QEMU smoke test.

Boots build/gemos.img headless (floppy + fresh IDE data disk, 128 MB, serial
log to a file, HMP monitor on a unix socket) and drives the desktop through
the monitor:

  * the kernel reaches "[BOOT] Window Manager Active",
  * the desktop screendump is a rendered 1920x1080 frame,
  * a mouse move over the empty desktop draws only the cursor again: no
    full frame (the kernel logs how many frames of each kind it drew),
    and no trace of the arrow on the wallpaper; typing into UTERM.ELF
    draws only partial frames, which leave the screen exactly as a full
    frame draws it,
  * the clock in the topbar shows a new minute by itself (QEMU's clock
    starts a few seconds before one), and the Log Viewer new log lines,
  * UTERM.ELF, ABOUT.ELF and UTEXTEDIT.ELF are started from the topbar menus,
    open their console window, close on Esc and are reaped with exit=0,
  * windows open between the topbar and the dock, a second one moves off
    the first one's corner, and a dragged window keeps its title bar there,
  * every character of a hosted console sits in its own cell of the grid,
  * the File Explorer starts ABOUT.ELF and USRSMOKE.ELF from GemFS, and a
    click on an entry leaves no drag outline behind,
  * every frame checked must appear on screen by itself: no input is sent
    while waiting for it,
  * the serial log has no PANIC, user fault, allocation failure or refused
    disk write, and no line was printed into the middle of another one;
    on a GemFS disk the kernel found nothing to reclaim at mount,
  * GemOS -> Shutdown turns the machine off (QEMU exits by itself) after
    the kernel has stopped GemFS; every mode but --selftest ends this way,
  * after QEMU has stopped, tools/mkgemfs check finds the GemFS data disk
    consistent.

The data disk is a fresh GemFS disk (tools/mkgemfs format), or with
--data-disk "unsigned" (random bytes) or "damaged" (a GemFS superblock with
a wrong checksum). --preboot boots once on the new disks and shuts down
before the test, which then runs on a second boot. --leftovers then adds
files that must survive and what a crash in the middle of writes and
deletes can leave: blocks and inodes that nothing uses. A boot during which
the blocks of one file cannot be mapped must free none of them and write
nothing; the test runs on the next boot, which must free exactly them,
after which tools/mkgemfs check finds nothing wrong. --slow-writes N lets
QEMU write only N requests per second to the data disk. --expect-unchanged
data|boot compares the SHA-256 of that disk image before and after the
test: the kernel must not have written a single byte.

With --matrix it runs the smoke test once per machine variant (32/64/256 MB
of RAM, no data disk, 4 MB of VRAM, boot from the hard disk image, a data
disk without GemFS or with a damaged superblock, a second boot, a slow
disk, a machine without ACPI, which Shutdown cannot turn off: the kernel
halts and says so on the screen, and a disk after a crash); each variant
also checks log lines that show which path the kernel took.

With --selftest it boots the self-test image (make selftest) instead, with
a second disk without GemFS: kernel/selftest.c checks the heap, the pool,
the ELF loader, GemFS and the file syscalls (also long ones, on a disk it
slows down), the FPU state and every exception a Ring 3 program can raise,
logs "[SELFTEST] RESULT", then
overflows its kernel stack on purpose; the log must end in the double
fault panic that names the overflow. The self-test boots twice on the same
disks: between the boots the harness checks the GemFS disk and the files
the first boot wrote, and the second boot checks those files again and
finds nothing to reclaim at mount. The
second disk must come out of both boots unchanged.

With --editor it tests UTEXTEDIT.ELF with files instead: Esc and the close
button ask before unsaved text is lost (a box lined up with the console
grid appears over the text, Esc cancels it), Y saves under a new name, the
File Explorer opens that file in the editor, which saves it again, N
discards, and saving over UTERM.ELF shows an error; the file is then read
on the host. It also pages and drags the Explorer scrollbar through a
110-entry directory, then opens the last file.

With --stress N it runs a load test instead: N cycles of opening all
three programs from the menus, typing into them and moving the mouse while
they start and exit, then closing them (Esc or the close button, taking
turns). Every process must be reaped with exit=0, the log must stay free of
failures and interleaved lines, and the desktop must be empty at the end.

Artifacts (serial log, QEMU output, PNG screenshots) go to --out.
Only the Python standard library is used.

Exit status: 0 = PASS, 1 = FAIL, 2 = setup error.
"""

import argparse
import hashlib
import importlib.machinery
import os
import random
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import types
import zlib

# The desktop runs at 1920x1080 with ui_scale 2.0 (kernel/kernel.c), so
# PS/2 mouse deltas and all coordinates below are logical (960x540) pixels.
# Smaller modes use ui_scale 1.0; --resolution changes both.
UI_SCALE = 2
SCREEN_SIZE = (1920, 1080)

# The colors used by the desktop and its widgets (kernel/ui/theme.h).
DESKTOP_RGB = (0x17, 0x18, 0x1A)
BAR_RGB = (0x20, 0x21, 0x23)
PAPER_RGB = (0xF1, 0xEF, 0xE8)
INK_RGB = (0x19, 0x1A, 0x1B)
ACCENT_RGB = (0xB4, 0x54, 0x36)
SELECTION_RGB = (0xEA, 0xDE, 0xD3)
FRAME_RGB = (0x8D, 0x89, 0x81)
ERROR_RGB = (0x95, 0x3F, 0x32)
DOCK_HEIGHT = 32


def set_resolution(width, height):
    global UI_SCALE, SCREEN_SIZE
    SCREEN_SIZE = (width, height)
    UI_SCALE = 2 if width >= 1920 and height >= 1080 else 1

# Machine variants for --matrix: name, extra arguments, log lines that
# must appear (regular expressions).
MATRIX = [
    ("floppy-128M", [], [r"\[GFX\] BGA page flipping enabled",
                         r"\[GemFS\] Mounted GemFS v3",
                         r"\[PROC\] Programs on GemFS: 4 seeded, 0 up to "
                         r"date"]),
    ("32M", ["--memory", "32"], [r"\[MEM\] Heap 0x"]),
    ("64M", ["--memory", "64"], [r"\[MEM\] Heap 0x"]),
    ("256M", ["--memory", "256"], [r"not used above 32 MB"]),
    ("no-data-disk", ["--no-data-disk"],
     [r"\[GemFS\] No GemFS disk", r"No file system: programs run"]),
    ("vga-4M", ["--vga-mem", "4", "--resolution", "1280x800"],
     [r"Resolution: 1280x800x32", r"page flip unavailable, using memcpy"]),
    ("hdd-boot", ["--boot", "hdd", "--expect-unchanged", "boot"],
     [r"\[BOOT\] Boot drive 0x00000080",
      r"\[GemFS\] [^\n]*: no GemFS signature, not mounted",
      r"\[GemFS\] Mounted GemFS v3"]),
    ("unsigned-disk", ["--data-disk", "unsigned", "--expect-unchanged", "data"],
     [r"\[GemFS\] [^\n]*: no GemFS signature, not mounted",
      r"\[GemFS\] No GemFS disk"]),
    ("damaged-superblock",
     ["--data-disk", "damaged", "--expect-unchanged", "data"],
     [r"\[GemFS\] [^\n]*: superblock checksum mismatch, not mounted",
      r"\[GemFS\] No GemFS disk"]),
    ("second-boot", ["--preboot", "--expect-unchanged", "data"],
     [r"\[GemFS\] Mounted GemFS v3",
      r"\[PROC\] Programs on GemFS: 0 seeded, 4 up to date"]),
    # every write takes about 80 ms, as on a busy host (CI once took longer
    # than the old ATA timeout to flush a new disk image)
    ("slow-disk", ["--slow-writes", "12"],
     [r"\[GemFS\] Mounted GemFS v3",
      r"\[PROC\] Programs on GemFS: 4 seeded, 0 up to date"]),
    # Shutdown cannot turn the machine off: the kernel halts instead
    ("no-acpi", ["--no-acpi"], [r"\[GemFS\] Mounted GemFS v3"]),
    # a crash left blocks and inodes that nothing uses: a boot that cannot
    # read the whole tree leaves them, the next one frees them
    ("after-crash", ["--preboot", "--leftovers"],
     [r"\[PROC\] Programs on GemFS: 0 seeded, 4 up to date"]),
]

# Topbar menus (kernel/gui/topbar/topbar.c): "GemOS" at x 10..80, "Apps" at
# x 90..160, bar height 28. Menus open at y=28 with 24 px items
# (kernel/ui/menu.h), so item i is centred at y = 29 + 24*i + 12.
MENU_GEMOS = (40, 13)
MENU_APPS = (121, 13)


def menu_item(x, index):
    return (x, 41 + 24 * index)


# (program, menu to open, item to click, whether its client area updates
# by itself: ABOUT.ELF redraws its uptime once a second through
# console_present, a redraw requested by a syscall, not by input)
APPS = [
    ("UTERM.ELF", MENU_APPS, menu_item(130, 1), False),    # Apps -> Terminal
    ("ABOUT.ELF", MENU_GEMOS, menu_item(60, 0), True),     # GemOS -> About
    ("UTEXTEDIT.ELF", MENU_APPS, menu_item(130, 3), False),  # Apps -> Text Editor
]


class Window:
    """A window as the kernel logs it: "[WM] Placed x= y= w= h=" for every
    window, "[CONSOLE] Opened PID= handle= x= y= w= h=" for a hosted app
    (kernel/gui/wm/wm.c places windows between the topbar and the dock)."""

    GEOMETRY = r"x=(\d+) y=(\d+) w=(\d+) h=(\d+)"

    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h

    @classmethod
    def parse(cls, match):
        return cls(*(int(v) for v in match.groups()[-4:]))

    def inside(self, area):
        return (self.x >= area.x and self.y >= area.y and
                self.x + self.w <= area.x + area.w and
                self.y + self.h <= area.y + area.h)

    def __str__(self):
        return "%dx%d at (%d,%d)" % (self.w, self.h, self.x, self.y)

    # Points on the window (kernel/gui/wm/wm.c: 1 px border, 26 px title
    # bar and 20 px controls with a 2 px gap).
    def title_bar(self):
        """On the title bar, away from the title text and the buttons."""
        return (self.x + self.w - 100, self.y + 3)

    def close_button(self):
        return (self.x + self.w - 15, self.y + 14)

    def minimize_button(self):
        return (self.x + self.w - 59, self.y + 14)

    def focus_rule(self):
        return (self.x + self.w - 100, self.y + 25)

    def client(self):
        return (self.x + 4, self.y + 28, self.x + self.w - 4,
                self.y + self.h - 4)

    def console_cell(self, row, col):
        """The middle of a hosted console's cell (kernel/console.c: 8x15
        cells after 8 px of padding), clear of a glyph that sticks out of
        its neighbour and of the cursor line at the bottom."""
        x = self.x + 9 + 8 * col
        y = self.y + 35 + 15 * row
        return (x + 2, y + 1, x + 6, y + 12)

    def inked_cells(self, image, row, cols):
        """The columns of a console row whose cell differs from the background."""
        def inked(col):
            area = image.logical_area(self.console_cell(row, col))
            return any(area[i:i + 3] != bytes(DESKTOP_RGB)
                       for i in range(0, len(area), 3))
        return [col for col in cols if inked(col)]

    def console_rows(self, first, last):
        """Rows of a hosted console (kernel/console.c: 8 px padding, 15 px
        lines), x0, y0, x1, y1 a pixel inside the cells."""
        return (self.x + 4, self.y + 36 + 15 * first, self.x + 680,
                self.y + 36 + 15 * (last + 1))


# The mouse moved over the empty desktop (logical points between the
# topbar and the dock): only the cursor may be drawn again.
CURSOR_PATH = [(480, 270), (800, 150), (150, 400), (600, 450), (300, 100),
               (700, 300)]
CURSOR_SIZE = (12, 17)  # the arrow (kernel/ui/cursor.c)
# "[GFX] Frames: full= partial= cursor=" (kernel/kernel.c), at most every
# 2 s
FRAMES_RE = re.compile(
    r"\[GFX\] Frames: full=(\d+) partial=(\d+) cursor=(\d+)")
FRAMES_LOG_DELAY = 2.5
# QEMU's clock starts this many seconds before a new minute, so the clock
# in the topbar (x from 80 px left of the right edge) changes then
CLOCK_FLIP = 14

# Stress test: what to type into each program (QEMU sendkey names; no Esc
# and no "q", which close windows).
STRESS_TYPING = {
    "UTERM.ELF": ["h", "e", "l", "p", "ret", "p", "i", "d", "ret",
                  "t", "i", "c", "k", "s", "ret", "a", "b", "c", "ret"],
    "ABOUT.ELF": ["a", "b", "c", "spc", "ret", "backspace"],
    "UTEXTEDIT.ELF": ["h", "e", "l", "l", "o", "spc", "g", "e", "m", "ret",
                      "x", "y", "z", "backspace", "ret"],
}
# Mouse path over the windows, the desktop and the dock (no clicks).
STRESS_WIGGLE = [(300, 200), (700, 300), (500, 480), (880, 520), (200, 400),
                 (640, 140), (60, 300)]


# --editor: UTEXTEDIT.ELF with files. Its console is 84x28 cells.
# Questions are drawn as a box over rows 8-12 and columns 6-77
# (userland/textedit/textedit_theme.h); the status line is row 26 and the
# shortcuts are row 27, the last one.
EDITOR_COLS = 84
EDITOR_BOX_ROWS = (8, 12)
EDITOR_BOX_COLS = (6, 77)
EDITOR_STATUS_ROWS = (26, 26)
EDITOR_MENU = (MENU_APPS, menu_item(130, 3))     # Apps -> Text Editor
EXPLORER_MENU = (MENU_APPS, menu_item(130, 0))   # Apps -> File Explorer
LOG_VIEWER_MENU = (MENU_APPS, menu_item(130, 2))  # Apps -> Log Viewer


EXPLORER_ROW_HEIGHT = 24


def explorer_list_y(window):
    # 27 px of window chrome, 32 px toolbar, 25 px column header.
    return window.y + 27 + 32 + 25


def explorer_entry(window, row):
    """Click inside a visible row of the Explorer."""
    return (window.x + 65, explorer_list_y(window) + row * EXPLORER_ROW_HEIGHT + 12)


EXPLORER_HOME = "/Home"
EXPLORER_USRSMOKE = 1  # /Home was created before the built-in programs.
EXPLORER_ABOUT = 3
EDITOR_NOTE_ROW = 1  # The home folder contains Projects and the saved file.
EXPLORER_DIR = EXPLORER_HOME + "/Projects"
EXPLORER_ITEMS = 110
EXPLORER_LAST_FILE = EXPLORER_DIR + "/NOTE-110.TXT"
RENAME_FOLDER = "My Work"
RENAME_DIR = EXPLORER_HOME + "/" + RENAME_FOLDER
RENAME_FILE = RENAME_DIR + "/final notes.txt"
NEW_TEXT_FILE = EXPLORER_HOME + "/New Text.txt"
RENAME_FILES = {"draft.txt": b"The original draft.\n",
                "taken.txt": b"Keep this other file.\n"}
EDITOR_FILE = EXPLORER_HOME + "/notes.txt"
EDITOR_SAVED = b"first line +more\nsecond"
# editing with the keyboard and the mouse (editor_features)
EDITOR_FEATURES_FILE = EXPLORER_HOME + "/features.txt"
EDITOR_FEATURES_SAVED = (b"alpha BETA gamma\n2nd line\nalpha  gamma\n"
                         b"second line")
# a file over the old 8 KB limit, opened, extended and saved again
EDITOR_BIG_FILE = "/big.txt"
EDITOR_BIG_TEXT = b"".join(b"line %05d of a file that the editor once could not open\n" % i
                           for i in range(1, 1601))
EDITOR_SELECTED_RGB = (0x7A, 0x3E, 0x2B)  # GEMOS_CONSOLE_STYLE_SELECTED
# QEMU sendkey names of the characters the test types
SENDKEY = {" ": "spc", "\n": "ret", ".": "dot", "/": "slash",
           "+": "shift-equal", "-": "minus"}


def key_names(text):
    return [SENDKEY.get(ch) or ("shift-" + ch.lower() if ch.isupper() else ch)
            for ch in text]


def pixels_changed(before, after):
    return sum(1 for i in range(0, len(before), 3)
               if before[i:i + 3] != after[i:i + 3])


def spans(cols):
    """Columns as ranges: [6, 7, 8, 77] -> "6-8, 77"."""
    parts = []
    for col in cols:
        if parts and parts[-1][1] == col - 1:
            parts[-1][1] = col
        else:
            parts.append([col, col])
    return ", ".join(str(a) if a == b else "%d-%d" % (a, b)
                     for a, b in parts) or "none"


def reddish_pixels(area):
    """Pixels of the error style (CONSOLE_ERROR_COLOR, a light red)."""
    return sum(1 for i in range(0, len(area), 3)
               if area[i] > 100 and area[i] > area[i + 1] + 50 and
               area[i] > area[i + 2] + 50)

# How long a frame may take to show up on its own (seconds). The first
# window of a font size is the slowest: its glyphs are rasterized on the
# first render (about 1 s under TCG).
DESKTOP_TIMEOUT = 20.0
WINDOW_TIMEOUT = 10.0

# The self-test: time for all of its checks, then for the deliberate
# kernel stack overflow to reach the double fault handler.
SELFTEST_TIMEOUT = 120.0
SELFTEST_PANIC_TIMEOUT = 30.0
SELFTEST_OVERFLOW = "[SELFTEST] Overflowing the kernel stack on purpose"
SELFTEST_DOUBLE_FAULT = re.compile(
    r"\[PANIC\] CPU Exception 8: Double Fault - kernel stack overflow "
    r"\(guard page\)\n(?:  .*\n)*System Halted\.")

FAILURE_PATTERNS = [
    r"PANIC",
    r"\[USERFAULT\]",
    r"Faulted PID",
    r"allocation failed",
    r"Alloc failed",
    r"Failed to",
    r"\[ELF\]",
    r"\[ATA\] (Refused|[a-z ]+ failed)",
    r"\[GemFS\] (Nothing reclaimed|Reclaiming stopped)",
]

# Kernel log lines start with a "[Tag] " prefix; indented lines continue the
# previous one. A tag after other text means that one line was printed into
# the middle of another.
LOG_TAG = re.compile(r"\[[A-Za-z][A-Za-z0-9_]*\] ")


class SmokeError(Exception):
    pass


MKGEMFS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mkgemfs")
DATA_DISK_SIZE = 10 * 1024 * 1024
SPARE_DISK_SIZE = 1024 * 1024

# Files the self-test writes on its first boot and checks on the second:
# PERSIST_TEXT in kernel/selftest.c, the FILETEST table in
# include/gemos/selftest_abi.h.
PERSIST_NOTE = "/persist/a/b/note.txt"
PERSIST_TEXT = b"Written by the GemOS self-test; the next boot reads it.\n"
FILETEST_BIG = "/fstest/big.bin"
FILETEST_TABLE = bytes((((i >> 8) ^ (i * 13 + 5)) & 0xFF)
                       for i in range(81920))

# --leftovers: files that must come out of the test as they went in, the
# second one with an indirect block (15 blocks of 4 KB), and what a crash
# left besides them (see add_leftovers).
KEPT_SMALL = "/kept/small.txt"
KEPT_BIG = "/kept/big.bin"
LEFTOVERS_RECLAIMED = ("[GemFS] Reclaimed 21 blocks and 4 inodes that no "
                       "file or directory used")


def mkgemfs(*args):
    return subprocess.run([sys.executable, MKGEMFS] + list(args),
                          capture_output=True)


def random_bytes(size, seed):
    """The same bytes on every run, so a failure can be repeated."""
    return random.Random(seed).randbytes(size)


def kept_files():
    return {KEPT_SMALL: b"Kept through a crash and the boot after it.\n",
            KEPT_BIG: random_bytes(60 * 1024, 0x4B45)}


def load_mkgemfs():
    """tools/mkgemfs as a module: it can change an image the way only a
    crash would."""
    loader = importlib.machinery.SourceFileLoader("mkgemfs", MKGEMFS)
    module = types.ModuleType(loader.name)
    loader.exec_module(module)
    return module


def add_leftovers(path):
    """Put the kept files on a GemFS disk, then leave what a crash in the
    middle of writes and deletes can leave (docs/GEMFS.md, "Update order"):
    blocks and inodes marked used that no directory leads to. That is 21
    blocks and 4 inodes: 3 blocks of a write that never got to its inode,
    and three deletes that stopped once the entry was gone: /lost.txt (2
    blocks), /lostdir with a file in it (2 blocks) and /lostbig.bin (13
    blocks and its indirect block). Returns the inode of KEPT_BIG."""
    gemfs = load_mkgemfs()
    fs = gemfs.GemFS(path, writable=True)
    try:
        fs.mkdir("/kept")
        for name, data in kept_files().items():
            fs.put(name, data)
        for _ in range(3):
            fs.alloc_block()
        fs.sync_bitmap()
        fs.put("/lost.txt", random_bytes(5000, 1))
        fs.mkdir("/lostdir")
        fs.put("/lostdir/inner.txt", b"In a directory nothing leads to.\n")
        fs.put("/lostbig.bin", random_bytes(13 * gemfs.BLOCK, 2))
        for name in ("/lost.txt", "/lostdir", "/lostbig.bin"):
            fs.remove_entry(gemfs.ROOT, fs.resolve(name))
        return fs.resolve(KEPT_BIG)
    finally:
        fs.close()


def set_indirect(path, number, block):
    """Point the indirect block of inode `number` at `block`; returns the
    block it pointed at."""
    gemfs = load_mkgemfs()
    fs = gemfs.GemFS(path, writable=True)
    try:
        inode = fs.read_inode(number)
        old, inode.indirect = inode.indirect, block
        fs.write_inode(number, inode)
        return old
    finally:
        fs.close()


def make_data_disk(path, kind):
    """A 10 MB data disk: a fresh GemFS disk (tools/mkgemfs format), random
    bytes without any signature, or a GemFS disk whose superblock checksum
    no longer matches (one byte of the label changed)."""
    if kind == "unsigned":
        with open(path, "wb") as f:
            f.write(random_bytes(DATA_DISK_SIZE, 0x6E05))
        return
    result = mkgemfs("format", path, "--size", "10M")
    if result.returncode != 0:
        raise SmokeError("mkgemfs format: %s"
                         % result.stderr.decode(errors="replace").strip())
    if kind == "damaged":
        with open(path, "r+b") as f:
            f.seek(48)  # first byte of the label, covered by the checksum
            byte = f.read(1)
            f.seek(48)
            f.write(bytes([byte[0] ^ 0x20]))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def title_bar_problem(image, window):
    color = image.logical_rgb(window.focus_rule())
    if color == ACCENT_RGB:
        return None
    return "active title rule is #%02x%02x%02x" % color


def interleaved_lines(text):
    bad = []
    for line in text.replace("\r", "").split("\n"):
        if not line.strip() or line.startswith("  "):
            continue
        tag = LOG_TAG.match(line)
        if not tag:
            bad.append(line)
        elif not line.startswith("[USER] ") and LOG_TAG.search(line, tag.end()):
            # [USER] lines carry text from user programs
            bad.append(line)
    return bad


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields = []
    pos = 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise SmokeError("unexpected screendump format %r" % fields[:4])
    width, height = int(fields[1]), int(fields[2])
    pixels = data[pos:pos + width * height * 3]
    if len(pixels) != width * height * 3:
        raise SmokeError("truncated screendump %s" % path)
    return width, height, pixels


def write_png(path, width, height, pixels):
    stride = width * 3
    raw = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride]
                   for y in range(height))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2,
                                           0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


class Image:
    def __init__(self, width, height, pixels):
        self.width = width
        self.height = height
        self.pixels = pixels

    def rgb(self, x, y):
        i = (y * self.width + x) * 3
        return tuple(self.pixels[i:i + 3])

    def logical_rgb(self, point):
        return self.rgb(point[0] * UI_SCALE, point[1] * UI_SCALE)

    def logical_area(self, box):
        x0, y0, x1, y1 = [v * UI_SCALE for v in box]
        return b"".join(
            self.pixels[(y * self.width + x0) * 3:(y * self.width + x1) * 3]
            for y in range(y0, y1))


class Monitor:
    """Minimal QEMU HMP client."""

    PROMPT = b"(qemu) "

    def __init__(self, address, timeout=30.0, process=None):
        deadline = time.monotonic() + timeout
        family = socket.AF_INET if isinstance(address, tuple) else socket.AF_UNIX
        while True:
            self.sock = socket.socket(family, socket.SOCK_STREAM)
            try:
                self.sock.connect(address)
                break
            except OSError:
                self.sock.close()
                if process is not None and process.poll() is not None:
                    raise SmokeError(f"QEMU launcher exited with status {process.returncode} before the monitor opened")
                if time.monotonic() > deadline:
                    raise SmokeError("QEMU monitor socket did not appear")
                time.sleep(0.1)
        self.sock.settimeout(15.0)
        self._read_until_prompt()

    def _read_until_prompt(self):
        data = b""
        while not data.endswith(self.PROMPT):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise SmokeError("QEMU monitor closed the connection")
            data += chunk
        return data

    def cmd(self, line):
        self.sock.sendall(line.encode() + b"\n")
        return self._read_until_prompt()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class Pointer:
    """Tracks the logical cursor position. QEMU queues only a few PS/2
    packets per event, so the pointer moves in small steps."""

    STEP = 40

    def __init__(self, monitor):
        self.monitor = monitor
        self.x = None
        self.y = None

    def home(self):
        logical = max(SCREEN_SIZE) // UI_SCALE
        for _ in range(logical // self.STEP + 6):
            self.monitor.cmd("mouse_move -%d -%d" % (self.STEP, self.STEP))
            time.sleep(0.02)
        self.x, self.y = 0, 0

    def move_to(self, point):
        dx, dy = point[0] - self.x, point[1] - self.y
        while dx or dy:
            sx = max(-self.STEP, min(self.STEP, dx))
            sy = max(-self.STEP, min(self.STEP, dy))
            self.monitor.cmd("mouse_move %d %d" % (sx, sy))
            time.sleep(0.02)
            dx -= sx
            dy -= sy
        self.x, self.y = point

    def click(self, button=1):
        self.press(button)
        self.release()

    def double_click(self):
        self.click()
        self.click()

    def press(self, button=1):
        self.monitor.cmd("mouse_button %d" % button)
        time.sleep(0.15)

    def release(self):
        self.monitor.cmd("mouse_button 0")
        time.sleep(0.15)


class Smoke:
    def __init__(self, args):
        self.args = args
        self.out = os.path.abspath(args.out)
        self.results = []
        self.qemu = None
        self.monitor = None
        self.sockdir = None
        self.serial_log = os.path.join(self.out, "serial.log")
        self.deadline = time.monotonic() + args.timeout
        self.disks = {}   # name -> image file: boot, data, spare
        self.hashes = {}  # name -> SHA-256 before the test

    # -- reporting ---------------------------------------------------------
    def check(self, ok, name, detail=""):
        self.results.append((ok, name, detail))
        line = "%s  %s" % ("PASS" if ok else "FAIL", name)
        if detail:
            line += "  (%s)" % detail
        print(line, flush=True)
        return ok

    # -- disks ---------------------------------------------------------------
    def prepare_disks(self):
        if not os.path.isfile(self.args.image):
            raise SmokeError("disk image %s not found, run make all"
                             % self.args.image)
        if os.path.isdir(self.out):
            shutil.rmtree(self.out)
        os.makedirs(self.out)
        self.disks["boot"] = os.path.join(self.out,
                                          os.path.basename(self.args.image))
        shutil.copyfile(self.args.image, self.disks["boot"])
        if not self.args.no_data_disk:
            self.disks["data"] = os.path.join(self.out, "data.img")
            make_data_disk(self.disks["data"], self.args.data_disk)
        if self.args.selftest:
            # a disk without GemFS: the ATA driver must refuse to write it
            self.disks["spare"] = os.path.join(self.out, "spare.img")
            with open(self.disks["spare"], "wb") as f:
                f.write(random_bytes(SPARE_DISK_SIZE, 0x5A5E))

    def prepare_explorer_directory(self):
        disk = self.disks.get("data")
        if not disk:
            raise SmokeError("the editor test needs a GemFS disk")
        note = os.path.join(self.out, "fixture.txt")
        with open(note, "w") as stream:
            stream.write("Explorer pagination test.\n")

        def require(*args):
            result = mkgemfs(*args)
            if result.returncode:
                raise SmokeError(result.stderr.decode(errors="replace").strip())

        require("mkdir", disk, EXPLORER_HOME)
        require("mkdir", disk, EXPLORER_DIR)
        big = os.path.join(self.out, "big.txt")
        with open(big, "wb") as stream:
            stream.write(EDITOR_BIG_TEXT)
        require("put", disk, big, EDITOR_BIG_FILE)
        for index in range(1, EXPLORER_ITEMS + 1):
            name = "NOTE-%03d.TXT" % index
            if index == 1:
                name = "NOTE-001-" + "W" * 40 + ".TXT"
            require("put", disk, note, EXPLORER_DIR + "/" + name)

    def prepare_explorer_names(self):
        disk = self.disks.get("data")
        if not disk or self.args.data_disk != "gemfs":
            raise SmokeError("the Explorer test needs a GemFS disk")
        mkgemfs("mkdir", disk, EXPLORER_HOME).check_returncode()
        mkgemfs("mkdir", disk, EXPLORER_HOME + "/Work").check_returncode()
        for name, data in RENAME_FILES.items():
            source = os.path.join(self.out, name)
            with open(source, "wb") as stream:
                stream.write(data)
            mkgemfs("put", disk, source, EXPLORER_HOME + "/Work/" + name).check_returncode()

    def snapshot_disks(self):
        self.hashes = {name: sha256(path) for name, path in self.disks.items()}

    def check_gemfs(self, when):
        if self.args.no_data_disk or self.args.data_disk != "gemfs":
            return True
        result = mkgemfs("check", self.disks["data"])
        output = result.stdout.decode(errors="replace").strip()
        errors = result.stderr.decode(errors="replace").strip()
        return self.check(result.returncode == 0,
                          "disk: tools/mkgemfs check %s" % when,
                          (output or errors).splitlines()[-1]
                          if (output or errors) else "")

    def check_disks(self):
        """After the last QEMU run: the GemFS disk is consistent, and the
        disks that had to stay untouched are byte for byte the same."""
        self.check_gemfs("after the test")
        if self.args.editor:
            self.check_editor_files()
        if self.args.leftovers:
            for path, data in kept_files().items():
                result = mkgemfs("cat", self.disks["data"], path)
                self.check(result.returncode == 0 and result.stdout == data,
                           "disk: %s is as it was before the crash" % path)
        unchanged = list(self.args.expect_unchanged)
        if self.args.selftest:
            unchanged.append("spare")
        for name in unchanged:
            if name not in self.disks:
                self.check(False, "disk: %s image unchanged" % name,
                           "no %s disk in this run" % name)
                continue
            after = sha256(self.disks[name])
            self.check(after == self.hashes[name],
                       "disk: %s image unchanged" % name,
                       "sha256 %s" % after[:16] if after == self.hashes[name]
                       else "sha256 %s before, %s after"
                       % (self.hashes[name][:16], after[:16]))

    def preboot(self):
        """Boot once on the new disks until the kernel has put its programs
        on GemFS, then shut down: the test runs on the second boot."""
        self.start_qemu("preboot-serial.log")
        try:
            done = self.wait_for(lambda t: "[PROC] Programs on GemFS" in t or
                                 "[PROC] No file system" in t,
                                 self.args.boot_timeout)
            self.check(bool(done), "preboot: the first boot seeded the "
                       "programs and was shut down")
        finally:
            self.stop_qemu()
        self.check_gemfs("after the first boot")

    def crash_leftovers(self):
        """Leave on the data disk what a crash can leave, and make the
        blocks of one file unreadable: its indirect block points outside
        the data area. The kernel cannot tell then what belongs to whom, so
        a boot must free nothing and write nothing. The test runs on the
        next boot, with the pointer back, which frees all the leftovers."""
        data = self.disks["data"]
        big = add_leftovers(data)
        indirect = set_indirect(data, big, 0)
        before = sha256(data)
        self.start_qemu("leftovers-serial.log")
        try:
            done = self.wait_for(lambda t: "[PROC] Programs on GemFS" in t,
                                 self.args.boot_timeout)
        finally:
            self.stop_qemu()
        line = re.search(r"\[GemFS\] Nothing reclaimed: [^\n]*",
                         self.log_text())
        self.check(bool(done) and line is not None and
                   "a bad block pointer" in line.group(0),
                   "leftovers: a boot that cannot map the blocks of a file "
                   "frees nothing", line.group(0) if line else "no such line")
        self.check(sha256(data) == before,
                   "leftovers: ... and writes nothing to the disk")
        set_indirect(data, big, indirect)

    # -- QEMU ----------------------------------------------------------------
    def start_qemu(self, serial="serial.log"):
        qemu = shutil.which(os.environ.get("QEMU", "qemu-system-i386"))
        if not qemu:
            raise SmokeError("qemu-system-i386 not found (set QEMU=...)")

        self.serial_log = os.path.join(self.out, serial)
        # unix socket paths are limited to ~108 bytes; keep it short
        self.sockdir = tempfile.mkdtemp(prefix="gemos-smoke-")
        sock = os.path.join(self.sockdir, "hmp.sock")
        cmd = [qemu]
        disk = "file=%s,if=ide,index=%d,format=raw"
        if self.args.boot == "hdd":
            # boot disk first; GemFS finds no superblock there and mounts
            # the data disk behind it
            cmd += ["-drive", disk % (self.disks["boot"], 0), "-boot", "c"]
            index = 1
        elif self.args.boot == "cdrom":
            cmd += ["-drive", "file=%s,if=ide,index=2,media=cdrom,readonly=on,format=raw"
                    % self.disks["boot"], "-boot", "d"]
            index = 0
        else:
            cmd += ["-drive", "file=%s,if=floppy,format=raw"
                    % self.disks["boot"]]
            index = 0
        for name in ("data", "spare"):
            if name in self.disks:
                drive = disk % (self.disks[name], index)
                if name == "data" and self.args.slow_writes:
                    drive += ",throttling.iops-write=%d" % self.args.slow_writes
                cmd += ["-drive", drive]
                index += 1
        if self.args.no_acpi:
            # no ACPI power management, so no port turns the machine off
            cmd += ["-machine", "pc,acpi=off"]
        if self.args.vga_mem:
            # (-global VGA.vgamem_mb leaves the machine without a VGA)
            cmd += ["-vga", "none",
                    "-device", "VGA,vgamem_mb=%d" % self.args.vga_mem]
        if self.args.cpu:
            cmd += ["-cpu", self.args.cpu]
        now = time.gmtime()
        cmd += ["-rtc", "base=%04d-%02d-%02dT%02d:%02d:%02d"
                % (now[:5] + (60 - CLOCK_FLIP,))]
        self.clock_flip_at = time.monotonic() + CLOCK_FLIP
        cmd += [
            "-m", "%dM" % self.args.memory,
            "-display", "none",
            "-serial", "file:%s" % self.serial_log,
            "-monitor", "unix:%s,server,nowait" % sock,
            "-no-reboot",
        ]
        print("qemu: %s" % " ".join(cmd), flush=True)
        self.qemu_log = open(os.path.join(
            self.out, serial.replace("serial", "qemu")), "wb")
        self.qemu = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                                     stdout=self.qemu_log,
                                     stderr=subprocess.STDOUT)
        self.monitor = Monitor(sock, process=self.qemu)
        self.pointer = Pointer(self.monitor)

    def stop_qemu(self):
        if self.monitor:
            try:
                self.monitor.sock.sendall(b"quit\n")
            except OSError:
                pass
            self.monitor.close()
        if self.qemu and self.qemu.poll() is None:
            try:
                self.qemu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.qemu.kill()
                self.qemu.wait()
        if self.sockdir:
            shutil.rmtree(self.sockdir, ignore_errors=True)
        self.monitor = None
        self.sockdir = None

    def log_text(self):
        try:
            with open(self.serial_log, "rb") as f:
                return f.read().decode("latin-1")
        except FileNotFoundError:
            return ""

    def wait_for(self, predicate, timeout):
        limit = min(time.monotonic() + timeout, self.deadline)
        while True:
            result = predicate(self.log_text())
            if result:
                return result
            if self.qemu.poll() is not None:
                raise SmokeError("QEMU exited with status %d"
                                 % self.qemu.returncode)
            if time.monotonic() > limit:
                return None
            time.sleep(0.2)

    def screendump(self, name, save=True):
        ppm = os.path.join(self.out, name + ".ppm")
        monitor_path = ppm.replace(os.sep, "/").replace("\\", "\\\\").replace('"', '\\"')
        self.monitor.cmd('screendump "%s"' % monitor_path)
        width, height, pixels = read_ppm(ppm)
        if save:
            write_png(os.path.join(self.out, name + ".png"), width, height,
                      pixels)
        os.remove(ppm)
        return Image(width, height, pixels)

    def watch_screen(self, shot, problem, timeout):
        """Take screendumps until problem(image) returns None or the timeout
        passes. No input is sent: the frame has to appear by itself."""
        start = time.monotonic()
        while True:
            image = self.screendump(shot, save=False)
            issue = problem(image)
            elapsed = time.monotonic() - start
            if issue is None or elapsed > timeout:
                write_png(os.path.join(self.out, shot + ".png"), image.width,
                          image.height, image.pixels)
                return issue, elapsed
            time.sleep(0.1)

    # -- test steps ----------------------------------------------------------
    def boot(self):
        start = time.monotonic()
        found = self.wait_for(
            lambda t: "[BOOT] Window Manager Active" in t, self.args.boot_timeout)
        return self.check(bool(found), "boot: [BOOT] Window Manager Active",
                          "%.1f s" % (time.monotonic() - start))

    def desktop(self):
        # logical points: topbar, dock (bottom of the screen), wallpaper
        width, height = (v // UI_SCALE for v in SCREEN_SIZE)
        topbar = (300, 5)
        dock = (width - 60, height - 10)
        wallpaper = (600, 300)

        def problem(image):
            bar = image.logical_rgb(topbar)
            wall = image.logical_rgb(wallpaper)
            if (image.width, image.height) != SCREEN_SIZE:
                return "screen is %dx%d" % (image.width, image.height)
            if bar != BAR_RGB:
                return "topbar pixel is #%02x%02x%02x" % bar
            if image.logical_rgb(dock) != BAR_RGB:
                return "dock pixel is #%02x%02x%02x" % image.logical_rgb(dock)
            if wall != DESKTOP_RGB:
                return "wallpaper pixel is #%02x%02x%02x" % wall
            return None

        issue, seconds = self.watch_screen("desktop", problem,
                                           DESKTOP_TIMEOUT)
        return self.check(issue is None, "desktop: screendump shows topbar, "
                          "wallpaper and dock",
                          issue or "desktop.png, %.1f s" % seconds)

    def frame_counts(self, wait=True):
        """Full, partial and cursor frames so far, from the last "[GFX]
        Frames" line once the GUI loop has had the time to log every frame
        drawn. Without wait, the last line as it is: enough for the full
        count when no full frame was drawn in the last two seconds."""
        if wait:
            time.sleep(FRAMES_LOG_DELAY)
        found = FRAMES_RE.findall(self.log_text())
        return tuple(int(v) for v in found[-1]) if found else (0, 0, 0)

    def cursor_frames(self):
        """A mouse move that changes nothing else draws only the cursor
        again: no full frame, and no trace of the arrow where it was (the
        wallpaper is a solid color)."""
        full0, partial0, cursor0 = self.frame_counts()
        self.pointer.home()
        for point in CURSOR_PATH:
            self.pointer.move_to(point)
        full1, partial1, cursor1 = self.frame_counts()
        # a new minute on the clock may take one partial frame
        self.check(full1 == full0 and partial1 - partial0 <= 1 and
                   cursor1 - cursor0 >= 10,
                   "frames: moving the mouse over the desktop draws only the "
                   "cursor", "%d full frames, %d partial, %d cursor frames"
                   % (full1 - full0, partial1 - partial0, cursor1 - cursor0))

        image = self.screendump("cursor-moved")
        s = UI_SCALE
        px, py = CURSOR_PATH[-1]
        x0, y0 = (px - 2) * s, (py - 2) * s  # the arrow, with a margin
        x1 = (px + CURSOR_SIZE[0] + 2) * s
        y1 = (py + CURSOR_SIZE[1] + 2) * s
        stride = image.width * 3
        bad = []
        for y in range(30 * s, (SCREEN_SIZE[1] // s - DOCK_HEIGHT - 2) * s):
            row = image.pixels[y * stride:(y + 1) * stride]
            if y0 <= y < y1:
                row = row[:x0 * 3] + row[x1 * 3:]
            if row != bytes(DESKTOP_RGB) * (len(row) // 3):
                bad.append(y // s)
        self.check(not bad, "frames: no trace of the cursor on the wallpaper",
                   "cursor-moved.png, %s" % (
                       "rows with other pixels: %s" % spans(sorted(set(bad)))
                       if bad else "solid desktop"))

    def clock(self):
        """The clock in the topbar shows the new minute by itself: the GUI
        loop asks for a frame when it changes. No input is sent, and
        nothing else on the empty desktop asks for one."""
        width = SCREEN_SIZE[0] // UI_SCALE
        area = (width - 82, 4, width - 20, 26)
        if time.monotonic() > self.clock_flip_at - 1.0:
            self.clock_flip_at += 60  # too late for this one: the next
        before = self.screendump("clock-before").logical_area(area)
        time.sleep(max(0.0, self.clock_flip_at - time.monotonic()))
        issue, seconds = self.watch_screen(
            "clock-after", lambda image: None
            if image.logical_area(area) != before else "the clock is the same",
            10.0)
        self.check(issue is None, "frames: the clock shows the new minute by "
                   "itself", "clock-before.png, clock-after.png, %s"
                   % (issue or "%.1f s after the minute" % seconds))

    def menu_hover_frames(self):
        """Hover over two items without replacing the whole frame."""
        samples = (menu_item(18, 0), menu_item(18, 1))

        def highlight_problem(image, item):
            colors = tuple(image.logical_rgb(point) for point in samples)
            expected = (SELECTION_RGB, PAPER_RGB)
            if item == 1:
                expected = expected[::-1]
            return None if colors == expected else "menu item highlight did not move"

        self.pointer.home()
        self.pointer.move_to(MENU_GEMOS)
        self.pointer.click()
        self.pointer.move_to(menu_item(60, 0))
        issue, _ = self.watch_screen(
            "menu-first", lambda image: highlight_problem(image, 0),
            self.args.app_timeout,
        )
        if self.check(issue is None, "menu: first item highlights", issue or ""):
            full0, partial0, _ = self.frame_counts()
            self.pointer.move_to(menu_item(60, 1))
            issue, _ = self.watch_screen(
                "menu-second", lambda image: highlight_problem(image, 1),
                self.args.app_timeout,
            )
            self.check(issue is None, "menu: the highlight moves to the second item",
                       issue or "")
            full1, partial1, _ = self.frame_counts()
            self.check(full1 == full0 and partial1 > partial0,
                       "frames: menu hover draws a partial frame",
                       "%d full, %d partial" % (full1 - full0, partial1 - partial0))
        self.pointer.move_to(MENU_APPS)
        self.pointer.click()
        issue, _ = self.watch_screen(
            "menu-switched",
            lambda image: None
            if image.logical_rgb((18, 41)) == DESKTOP_RGB
            and image.logical_rgb((95, 41)) == PAPER_RGB
            else "Apps did not replace the GemOS menu",
            self.args.app_timeout,
        )
        self.check(issue is None, "menu: a single click switches the open menu",
                   issue or "")
        self.monitor.cmd("sendkey esc")

    def log_viewer(self):
        """The Log Viewer shows new log lines by itself: the GUI loop asks
        it once a second whether the log grew, and the kernel logs
        "[GFX] Frames" lines meanwhile. No input is sent while the test
        waits; then the close button closes the window."""
        placed_re = re.compile(r"\[WM\] Placed " + Window.GEOMETRY)
        before = len(placed_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(LOG_VIEWER_MENU[0])
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(LOG_VIEWER_MENU[1])
        self.pointer.click()
        placed = self.wait_for(lambda t: list(placed_re.finditer(t))[before:],
                               self.args.app_timeout)
        if not placed:
            raise SmokeError("the Log Viewer opened no window")
        window = Window.parse(placed[0])
        # the first frame with the window rasterizes its font (up to 2 s
        # under TCG): the log is compared once it is on the screen and its
        # frames are logged
        issue, _ = self.watch_screen(
            "log-viewer", lambda image: title_bar_problem(image, window),
            WINDOW_TIMEOUT)
        if issue:
            raise SmokeError("the Log Viewer window is not on the screen: %s"
                             % issue)
        time.sleep(FRAMES_LOG_DELAY)
        first = self.screendump("log-viewer", save=False).logical_area(
            window.client())
        issue, seconds = self.watch_screen(
            "log-viewer", lambda image: None
            if image.logical_area(window.client()) != first
            else "the log in the window is the same", 6.0)
        self.check(issue is None, "frames: the Log Viewer shows new log lines "
                   "by itself", "log-viewer.png, %s"
                   % (issue or "%.1f s, no input" % seconds))
        self.pointer.home()
        self.pointer.move_to(window.close_button())
        self.pointer.click()

    def full_frame_reference(self, name):
        """A screendump of a full frame: Esc in an open menu is the one
        input that still draws one (kernel/kernel.c). The pointer goes back
        where it was, so both pictures have the cursor at the same place."""
        back = (self.pointer.x, self.pointer.y)
        full0, _, _ = self.frame_counts(wait=False)
        self.pointer.move_to(MENU_GEMOS)
        self.pointer.click()
        time.sleep(0.4)
        self.monitor.cmd("sendkey esc")
        time.sleep(0.4)
        self.pointer.move_to(back)
        full1, _, _ = self.frame_counts()
        if full1 <= full0:
            raise SmokeError("frames: Esc in the GemOS menu drew no full frame")
        return self.screendump(name)

    def same_as_full_frame(self, partial, full):
        """Pixels that differ between two screendumps, without the clock
        (it may have shown a new minute meanwhile)."""
        width = SCREEN_SIZE[0] // UI_SCALE
        height = SCREEN_SIZE[1] // UI_SCALE
        return (pixels_changed(partial.logical_area((0, 0, width - 90, 28)),
                               full.logical_area((0, 0, width - 90, 28))) +
                pixels_changed(partial.logical_area((0, 28, width, height)),
                               full.logical_area((0, 28, width, height))))

    def partial_frames(self):
        """Typing into UTERM.ELF draws only the window that changed: no
        full frame, and the screen is then exactly what a full frame draws
        (full_frame_reference)."""
        name, menu, item, _ = APPS[0]
        opened_re = re.compile(r"\[CONSOLE\] Opened PID=(\d+) handle=\d+ " +
                               Window.GEOMETRY)
        before = len(opened_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(menu)
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(item)
        self.pointer.click()
        opened = self.wait_for(lambda t: list(opened_re.finditer(t))[before:],
                               self.args.app_timeout)
        if not opened:
            raise SmokeError("frames: %s opened no window" % name)
        pid, window = opened[0].group(1), Window.parse(opened[0])
        self.pointer.home()
        self.pointer.move_to(window.title_bar())
        full0, partial0, _ = self.frame_counts()
        for key in ["h", "e", "l", "p", "ret"]:
            self.monitor.cmd("sendkey %s" % key)
            time.sleep(0.05)
        full1, partial1, _ = self.frame_counts()
        self.check(full1 == full0 and partial1 > partial0,
                   "frames: typing into %s draws only partial frames" % name,
                   "%d full, %d partial frames"
                   % (full1 - full0, partial1 - partial0))
        partial = self.screendump("partial-frames")
        full = self.full_frame_reference("full-frame")
        differ = self.same_as_full_frame(partial, full)
        self.check(differ == 0, "frames: the partial frames drew what a full "
                   "frame draws", "partial-frames.png, full-frame.png, %d "
                   "pixels differ" % differ)
        self.monitor.cmd("sendkey esc")
        if not self.wait_for(lambda t: "[PROC] Reaped PID=%s exit=0" % pid in t,
                             self.args.app_timeout):
            raise SmokeError("frames: %s (PID=%s) did not end with exit=0"
                             % (name, pid))

    def console_window(self, pid):
        """The window of a hosted app, from its "[CONSOLE] Opened" line."""
        match = re.search(r"\[CONSOLE\] Opened PID=%s handle=\d+ " % pid +
                          Window.GEOMETRY, self.log_text())
        if not match:
            raise SmokeError("no window geometry in the log for PID=%s" % pid)
        return Window.parse(match)

    def run_app(self, name, menu, item, updates_itself):
        def press():
            self.pointer.home()
            self.pointer.move_to(menu)
            self.pointer.click()
            time.sleep(0.4)
            self.pointer.move_to(item)
            self.pointer.press()
        return self.run_program(name, press, updates_itself)

    def run_program(self, name, press, updates_itself, shot=None):
        """press() presses the mouse button where it starts the program
        name; then the program opens its window, closes on Esc and is
        reaped with exit=0."""
        spawn_re = re.compile(r"\[PROC\] Spawned PID=(\d+) " + re.escape(name))

        def spawned(text):
            return spawn_re.findall(text)

        before = len(spawned(self.log_text()))
        # The menu and the File Explorer start the program on the button
        # press. The button stays down until the window check is done, so
        # no input event can make the kernel redraw while the test waits
        # for the window.
        press()
        try:
            pids = self.wait_for(
                lambda t: spawned(t) if len(spawned(t)) > before else None,
                self.args.app_timeout)
            if not self.check(bool(pids), "%s: spawned" % name):
                return False
            pid = pids[-1]

            opened = self.wait_for(
                lambda t: "[CONSOLE] Opened PID=%s handle=" % pid in t,
                self.args.app_timeout)
            if not self.check(bool(opened), "%s: console window opened" % name,
                              "PID=%s" % pid):
                return False

            window = self.console_window(pid)
            self.check_window_drawn(name, window, shot)
        finally:
            try:
                self.pointer.release()
            except (OSError, SmokeError):
                pass  # QEMU is gone; the original error is reported

        if updates_itself:
            self.check_redraws_by_itself(name, window)

        self.monitor.cmd("sendkey esc")
        reap_re = re.compile(r"\[PROC\] Reaped PID=%s exit=(\d+)" % pid)
        reaped = self.wait_for(lambda t: reap_re.search(t),
                               self.args.app_timeout)
        if not reaped:
            return self.check(False, "%s: closed with Esc and reaped" % name,
                              "no Reaped line for PID=%s" % pid)
        return self.check(reaped.group(1) == "0",
                          "%s: closed with Esc and reaped" % name,
                          "PID=%s exit=%s" % (pid, reaped.group(1)))

    def check_redraws_by_itself(self, name, window):
        # Baseline: a frame that shows the window. The button release causes
        # one more frame, which a kernel that drops syscall redraw requests
        # would draw too, so the baseline must not be older than that frame.
        area = window.client()
        time.sleep(0.3)
        issue, _ = self.watch_screen(
            "updating", lambda image: title_bar_problem(image, window),
            WINDOW_TIMEOUT)
        if issue:
            return self.check(False, "%s: window redraws by itself" % name,
                              "window not on screen: %s" % issue)
        time.sleep(0.3)
        first = self.screendump("updating", save=False).logical_area(area)
        start = time.monotonic()
        while time.monotonic() - start < 5.0:
            time.sleep(0.5)
            if self.screendump("updating", save=False).logical_area(area) != first:
                return self.check(True, "%s: window redraws by itself" % name,
                                  "content changed after %.1f s, no input"
                                  % (time.monotonic() - start))
        return self.check(False, "%s: window redraws by itself" % name,
                          "unchanged for 5 s without input")

    def check_window_drawn(self, name, window, shot=None):
        shot = shot or name.split(".")[0].lower()

        issue, seconds = self.watch_screen(
            shot, lambda image: title_bar_problem(image, window),
            WINDOW_TIMEOUT)
        return self.check(issue is None,
                          "%s: window title bar on screen" % name,
                          "%s.png, %s" % (shot, issue or
                                          "%.1f s, no input" % seconds))

    # -- window placement ------------------------------------------------------
    def open_hosted_app(self, name, menu, item):
        """Start an app from a menu and return its PID and window."""
        opened_re = re.compile(r"\[CONSOLE\] Opened PID=(\d+) handle=\d+ " +
                               Window.GEOMETRY)
        before = len(opened_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(menu)
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(item)
        self.pointer.click()
        opened = self.wait_for(lambda t: list(opened_re.finditer(t))[before:],
                               self.args.app_timeout)
        if not opened:
            raise SmokeError("windows: %s opened no window" % name)
        return opened[0].group(1), Window.parse(opened[0])

    def open_about(self):
        return self.open_hosted_app(*APPS[1][:3])

    def drag_title_bar(self, window, point):
        """Drag the window by its title bar until the mouse is at point; the
        window the kernel logs when the button is released there, None if
        it logs none."""
        dropped_re = re.compile(r"\[WM\] Dropped " + Window.GEOMETRY)
        before = len(dropped_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(window.title_bar())
        self.pointer.press()
        self.pointer.move_to(point)
        time.sleep(0.3)
        self.pointer.release()
        dropped = self.wait_for(lambda t: list(dropped_re.finditer(t))[before:],
                                5.0)
        return Window.parse(dropped[0]) if dropped else None

    def windows(self):
        """Windows stay between the topbar and the dock (kernel/gui/wm)."""
        area_match = re.search(r"\[WM\] Work area " + Window.GEOMETRY,
                               self.log_text())
        if not self.check(area_match is not None, "windows: work area logged"):
            return
        area = Window.parse(area_match)
        self.check(True, "windows: work area", str(area))

        first_pid, first = self.open_about()
        second_pid, second = self.open_about()
        self.check(second.x == first.x + 24 and second.y == first.y + 24,
                   "windows: a second ABOUT.ELF window moves off the first "
                   "one's corner", "%s, then %s" % (first, second))

        bottom = area.y + area.h - 27  # lowest top: the title bar stays
        height = SCREEN_SIZE[1] // UI_SCALE
        dropped = self.drag_title_bar(second, (second.title_bar()[0],
                                               height - DOCK_HEIGHT // 2))
        self.check(dropped is not None and dropped.y == bottom,
                   "windows: dragged onto the dock, the title bar stays "
                   "above it", "dropped %s" % dropped)
        if dropped is not None:
            # release over the topbar: it takes the click, but the drag ends
            dropped = self.drag_title_bar(dropped, (dropped.title_bar()[0], 10))
            self.check(dropped is not None and dropped.y == area.y,
                       "windows: a drag released over the topbar ends there",
                       "dropped %s" % dropped)

        for pid in (second_pid, first_pid):  # the focused one closes first
            self.monitor.cmd("sendkey esc")
            if not self.wait_for(
                    lambda t, p=pid: "[PROC] Reaped PID=%s " % p in t,
                    self.args.app_timeout):
                raise SmokeError("windows: ABOUT.ELF (PID=%s) did not close"
                                 % pid)

        placed = [Window.parse(m) for m in re.finditer(
            r"\[WM\] Placed " + Window.GEOMETRY, self.log_text())]
        outside = [str(w) for w in placed if not w.inside(area)]
        self.check(bool(placed) and not outside,
                   "windows: every window placed inside the work area",
                   "outside: %s" % ", ".join(outside[:3]) if outside
                   else "%d windows" % len(placed))

    def console_grid(self):
        """Every character of a hosted console sits in its own cell: typed
        text of wide and narrow letters fills exactly as many cells."""
        typed = "WiWiWiWiWi"
        row, col = 2, 2  # UTEXTEDIT's body (userland/textedit/textedit_theme.h)
        pid, window = self.editor_from_menu()
        if pid is None:
            return
        self.type_text(typed)
        time.sleep(1.0)
        image = self.screendump("console-grid")
        inked = window.inked_cells(image, row,
                                   range(col, col + len(typed) + 8))
        expected = list(range(col, col + len(typed)))
        self.check(inked == expected,
                   "console: %d letters fill exactly %d cells" %
                   (len(typed), len(typed)),
                   "console-grid.png, cells with ink in row %d: %s" %
                   (row, inked))
        since = len(self.log_text())
        self.monitor.cmd("sendkey esc")
        time.sleep(0.5)
        self.monitor.cmd("sendkey n")
        self.editor_ended(pid, "the grid test closes it", since)

    # -- File Explorer -----------------------------------------------------------
    def window_focus(self):
        """A hidden Explorer cannot close itself or keep the Terminal
        unfocused. Opening and closing windows, the dock, minimizing and
        focus changes draw no full frame, and the screen after them is what
        a full frame draws."""
        full0, partial0, _ = self.frame_counts(wait=False)
        explorer = self.open_explorer()
        pid, terminal = self.open_hosted_app(*APPS[0][:3])
        width, height = (v // UI_SCALE for v in SCREEN_SIZE)
        dock = (16, height - DOCK_HEIGHT // 2)  # Explorer was registered first
        outside = (width - 20, 48)

        def click(point):
            self.pointer.move_to(point)
            self.pointer.click()

        click(dock)
        self.check_window_drawn("focus: Explorer selected in the dock", explorer,
                                "focus-before-minimize")
        click(explorer.minimize_button())
        self.pointer.move_to(outside)
        if not self.check_window_drawn("focus: Terminal after minimizing Explorer",
                                       terminal, "focus-after-minimize"):
            raise SmokeError("minimizing an active window did not transfer focus")

        area = terminal.client()
        before = self.screendump("focus-baseline", save=False).logical_area(area)
        since = len(self.log_text())
        click(explorer.close_button())  # the hidden window used to close here
        self.pointer.move_to(outside)
        self.type_text("help\n")
        issue, _ = self.watch_screen(
            "focus-terminal-input",
            lambda image: None if title_bar_problem(image, terminal) is None
            and image.logical_area(area) != before
            else "the terminal did not receive input",
            self.args.app_timeout,
        )
        self.check(issue is None and "[WM] Close Window" not in self.log_text()[since:],
                   "focus: the click and keyboard input reach the visible Terminal",
                   issue or "")

        # Close the Terminal while Explorer is still hidden, then click
        # the wallpaper at the hidden window's former close button.
        self.monitor.cmd("sendkey esc")
        ended = self.wait_for(lambda text: "[PROC] Reaped PID=%s exit=0" % pid in text,
                              self.args.app_timeout)
        if not self.check(bool(ended), "focus: Esc closes the focused Terminal"):
            raise SmokeError("could not reach the desktop with only Explorer minimized")
        since = len(self.log_text())
        click(explorer.close_button())
        click(dock)
        restored = self.check_window_drawn("focus: Explorer restored from the dock",
                                           explorer, "focus-restored")
        self.check(restored and "[WM] Close Window" not in self.log_text()[since:],
                   "focus: a click on the wallpaper leaves the hidden Explorer intact")
        full1, partial1, _ = self.frame_counts()
        self.check(full1 == full0 and partial1 > partial0,
                   "frames: opening, focusing, minimizing, restoring and "
                   "closing windows draw only partial frames",
                   "%d full, %d partial frames" % (full1 - full0,
                                                   partial1 - partial0))
        if restored:
            self.pointer.move_to(outside)
            partial = self.screendump("clicks-partial")
            differ = self.same_as_full_frame(
                partial, self.full_frame_reference("clicks-full"))
            self.check(differ == 0, "frames: after the clicks the screen is "
                       "what a full frame draws",
                       "clicks-partial.png, clicks-full.png, %d pixels differ"
                       % differ)
            click(explorer.close_button())

    def wait_explorer_path(self, path):
        opened = re.compile(r"\[EXPLORER\] Opened (.*)\r?\n")

        def listed(text):
            paths = opened.findall(text)
            return paths and paths[-1].rstrip("\r") == path

        if not self.wait_for(listed, self.args.app_timeout):
            raise SmokeError("Explorer did not list " + path)

    def explorer_up(self, window):
        self.pointer.move_to((window.x + 27, window.y + 42))
        self.pointer.click()

    def open_explorer(self, root=False):
        """Open File Explorer in Home. Use Up to reach programs at the root."""
        placed_re = re.compile(r"\[WM\] Placed " + Window.GEOMETRY)
        before = len(placed_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(EXPLORER_MENU[0])
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(EXPLORER_MENU[1])
        self.pointer.click()
        placed = self.wait_for(lambda t: list(placed_re.finditer(t))[before:],
                               self.args.app_timeout)
        if not placed:
            raise SmokeError("the File Explorer opened no window")
        window = Window.parse(placed[0])
        self.wait_explorer_path(EXPLORER_HOME)
        if not self.check_window_drawn("File Explorer in Home", window, "explorer-ready"):
            raise SmokeError("File Explorer did not draw its home window")
        if root:
            self.explorer_up(window)
            self.wait_explorer_path("/")
        return window

    def file_explorer(self):
        """Select a row and launch a program from the disk."""
        if "[GemFS] Mounted" not in self.log_text():
            print("SKIP  explorer: no file system, no programs to start",
                  flush=True)
            return
        self.window_focus()
        explorer = self.open_explorer(root=True)
        about = explorer_entry(explorer, EXPLORER_ABOUT)
        self.pointer.home()
        self.pointer.move_to(about)
        self.pointer.click()                   # select
        self.pointer.move_to((explorer.x + 40, explorer.y + explorer.h - 60))
        issue, _ = self.watch_screen(
            "explorer-selected",
            lambda image: None
            if image.logical_rgb((explorer.x + 1, about[1])) == ACCENT_RGB
            and image.logical_rgb((explorer.x + 5, about[1])) == SELECTION_RGB
            else "the selected row lost its marker after release",
            WINDOW_TIMEOUT,
        )
        self.check(issue is None, "explorer: a click selects the ABOUT.ELF row",
                   issue or "explorer-selected.png")

        def open_about():
            self.pointer.move_to(about)
            self.pointer.click()
            self.pointer.press()
        self.run_program("/ABOUT.ELF", open_about, False,
                         shot="explorer-about")

        # a program without a window
        spawn_re = re.compile(r"\[PROC\] Spawned PID=(\d+) /USRSMOKE\.ELF")
        before = len(spawn_re.findall(self.log_text()))
        self.pointer.move_to(explorer_entry(explorer, EXPLORER_USRSMOKE))
        self.pointer.double_click()
        pids = self.wait_for(lambda t: spawn_re.findall(t)[before:],
                             self.args.app_timeout)
        reaped = pids and self.wait_for(
            lambda t: re.search(r"\[PROC\] Reaped PID=%s exit=(-?\d+)"
                                % pids[0], t), self.args.app_timeout)
        self.check(bool(reaped) and reaped.group(1) == "42",
                   "explorer: /USRSMOKE.ELF, a program without a window, "
                   "runs and exits", "PID=%s exit=%s"
                   % (pids[0] if pids else "none",
                      reaped.group(1) if reaped else "none"))

    # -- stress test -----------------------------------------------------------
    def burst(self, keys):
        """Input while programs start or exit: small mouse moves and keys."""
        for key in keys:
            self.monitor.cmd("mouse_move 3 2")
            self.monitor.cmd("sendkey %s" % key)
            self.monitor.cmd("mouse_move -3 -2")
            time.sleep(0.03)

    def stress_open(self, name, menu, item):
        opened_re = re.compile(r"\[CONSOLE\] Opened PID=(\d+) ")
        before = len(opened_re.findall(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(menu)
        self.pointer.click()
        time.sleep(0.3)
        self.pointer.move_to(item)
        self.pointer.press()
        self.burst(["a", "b"])        # typed while the program starts
        self.pointer.release()
        opened = self.wait_for(
            lambda t: opened_re.findall(t)[before:], self.args.app_timeout)
        if not opened:
            raise SmokeError("stress: %s did not open a window" % name)
        for key in STRESS_TYPING[name]:
            self.monitor.cmd("sendkey %s" % key)
            time.sleep(0.02)
        return opened[0]

    def stress(self):
        cycles = self.args.stress
        start = time.monotonic()
        spawn_re = re.compile(r"\[PROC\] Spawned PID=(\d+) ")
        reap_re = re.compile(r"\[PROC\] Reaped PID=(\d+) exit=(-?\d+)")
        for cycle in range(cycles):
            if time.monotonic() > self.deadline:
                raise SmokeError("stress: time budget used up after %d cycles"
                                 % cycle)
            pids = [self.stress_open(name, menu, item)
                    for name, menu, item, _ in APPS]
            windows = [self.console_window(pid) for pid in pids]
            for point in STRESS_WIGGLE:
                self.pointer.move_to(point)
            # close from the top window down, alternating Esc and the button
            for (name, _, _, _), pid, window in reversed(
                    list(zip(APPS, pids, windows))):
                if cycle % 2 == 0:
                    self.monitor.cmd("sendkey esc")
                else:
                    self.pointer.home()
                    self.pointer.move_to(window.close_button())
                    self.pointer.click()
                if name == "UTEXTEDIT.ELF":
                    # it asks about the text typed into it: discard
                    self.monitor.cmd("sendkey n")
                self.burst(["x", "y"])    # goes to the next window down
                if not self.wait_for(
                        lambda t, p=pid: "[PROC] Reaped PID=%s " % p in t,
                        self.args.app_timeout):
                    raise SmokeError("stress: cycle %d, %s (PID=%s) was not "
                                     "reaped" % (cycle + 1, name, pid))
            print("  cycle %d/%d done (%.0f s)" % (cycle + 1, cycles,
                                                   time.monotonic() - start),
                  flush=True)

        text = self.log_text()
        pids = spawn_re.findall(text)
        reaped = dict(reap_re.findall(text))
        opened = re.findall(r"\[CONSOLE\] Opened PID=(\d+) ", text)
        self.check(len(pids) == 3 * cycles,
                   "stress: %d programs started" % (3 * cycles),
                   "%d Spawned lines" % len(pids))
        self.check(sorted(opened) == sorted(pids),
                   "stress: every program opened its window",
                   "%d of %d" % (len(opened), len(pids)))
        missing = [p for p in pids if p not in reaped]
        bad_exit = ["%s:%s" % (p, reaped[p]) for p in pids
                    if p in reaped and reaped[p] != "0"]
        self.check(not missing and not bad_exit,
                   "stress: every program reaped with exit=0",
                   "missing %s, exit %s" % (missing[:5], bad_exit[:5])
                   if missing or bad_exit else "%d reaped" % len(pids))

        def no_window(image):
            for window in windows:
                if title_bar_problem(image, window) is None:
                    return "a window is still open at %s" % window
            return None

        time.sleep(1.0)
        issue, _ = self.watch_screen("stress-end", no_window, 5.0)
        self.check(issue is None, "stress: desktop empty at the end",
                   issue or "stress-end.png")
        self.check(True, "stress: %d cycles" % cycles,
                   "%.0f s" % (time.monotonic() - start))

    # -- text editor -----------------------------------------------------------
    def type_text(self, text):
        for name in key_names(text):
            self.monitor.cmd("sendkey %s" % name)
            time.sleep(0.03)

    def start_editor(self, start, what):
        """Start UTEXTEDIT.ELF with start() (a click somewhere); its PID and
        window once the window is on the screen, (None, None) if it did not
        get there."""
        spawn_re = re.compile(r"\[PROC\] Spawned PID=(\d+) UTEXTEDIT\.ELF")
        before = len(spawn_re.findall(self.log_text()))
        start()
        pids = self.wait_for(lambda t: spawn_re.findall(t)[before:],
                             self.args.app_timeout)
        if not self.check(bool(pids), "editor: %s starts UTEXTEDIT.ELF" % what):
            return None, None
        pid = pids[0]
        opened = self.wait_for(
            lambda t: "[CONSOLE] Opened PID=%s handle=" % pid in t,
            self.args.app_timeout)
        if not self.check(bool(opened), "editor: its window opened",
                          "PID=%s" % pid):
            return None, None
        window = self.console_window(pid)
        if not self.check_window_drawn("UTEXTEDIT.ELF", window):
            return None, None
        time.sleep(0.5)  # the first frame of the editor itself
        return pid, window

    def editor_from_menu(self):
        def click():
            self.pointer.home()
            self.pointer.move_to(EDITOR_MENU[0])
            self.pointer.click()
            time.sleep(0.4)
            self.pointer.move_to(EDITOR_MENU[1])
            self.pointer.click()
        return self.start_editor(click, "Apps -> Text Editor")

    def editor_exited(self, pid):
        return "[PROC] Reaped PID=%s " % pid in self.log_text()

    def editor_ended(self, pid, what, since):
        """The editor ended with exit=0 in the log after offset since: the
        length of the log before the key or click that should end it."""
        reap_re = re.compile(r"\[PROC\] Reaped PID=%s exit=(-?\d+)" % pid)
        reaped = self.wait_for(lambda t: reap_re.search(t, since),
                               self.args.app_timeout)
        return self.check(bool(reaped) and reaped.group(1) == "0",
                          "editor: %s" % what,
                          "PID=%s exit=%s" % (pid, reaped.group(1) if reaped
                                               else "none"))

    def editor_running(self, pid, what):
        time.sleep(1.5)
        return self.check(not self.editor_exited(pid), "editor: %s" % what,
                          "PID=%s still running" % pid)

    def editor_box(self, shot, pid, window, reference, shown, what):
        """Wait, without input, until the question box is on the screen
        (the box area differs from reference) or gone again. A window that
        closed changes the area too, so the editor must still run."""
        box = window.console_rows(*EDITOR_BOX_ROWS)

        def problem(image):
            if self.editor_exited(pid):
                return "UTEXTEDIT.ELF (PID=%s) has exited" % pid
            changed = pixels_changed(reference, image.logical_area(box))
            if shown and changed < 400:
                return "box area unchanged (%d pixels)" % changed
            if not shown and changed > 50:
                return "box area still differs (%d pixels)" % changed
            return None

        issue, seconds = self.watch_screen(shot, problem, WINDOW_TIMEOUT)
        return self.check(issue is None, "editor: %s" % what,
                          "%s.png, %s" % (shot, issue or "%.1f s" % seconds))

    def question_box_grid(self, window):
        """The question box lines up with the console grid: its lines of '-'
        fill its columns, and its empty row (the "Save the changes"
        question has no file name line) has a bar at either end."""
        first, last = EDITOR_BOX_COLS
        top, bottom = EDITOR_BOX_ROWS
        middle = (top + bottom) // 2
        image = self.screendump("editor-box-grid")
        wanted = {top: list(range(first, last + 1)), middle: [first, last],
                  bottom: list(range(first, last + 1))}
        found = {row: window.inked_cells(image, row, range(EDITOR_COLS))
                 for row in wanted}
        self.check(found == wanted,
                   "editor: the question box lines up with the grid",
                   "editor-box-grid.png, cells with ink: " +
                   "; ".join("row %d: %s" % (row, spans(found[row]))
                             for row in sorted(found)))

    def editor(self):
        start = time.monotonic()

        # a new document: Esc and the close button ask before closing
        pid, window = self.editor_from_menu()
        if pid is None:
            return
        box = window.console_rows(*EDITOR_BOX_ROWS)
        self.type_text("first line\nsecond")
        time.sleep(1.0)
        before = self.screendump("editor-typed").logical_area(box)
        self.monitor.cmd("sendkey esc")
        self.editor_box("editor-question", pid, window, before, True,
                        "Esc with unsaved text asks first")
        self.question_box_grid(window)
        self.editor_running(pid, "the window stays while it asks")
        self.monitor.cmd("sendkey esc")
        self.editor_box("editor-cancelled", pid, window, before, False,
                        "Esc in the question cancels it")
        self.pointer.home()
        self.pointer.move_to(window.close_button())
        self.pointer.click()
        self.editor_box("editor-close-button", pid, window, before, True,
                        "the close button asks as well")
        self.editor_running(pid, "the close button did not close it yet")

        # Y: save it; there is no file yet, so it asks for a name first
        since = len(self.log_text())
        self.monitor.cmd("sendkey y")
        time.sleep(0.5)
        self.type_text("notes")        # after the "~/" it offers
        self.monitor.cmd("sendkey ret")
        if not self.editor_ended(pid, "Y, then Save as \"notes\": saved and "
                                 "closed", since):
            return

        # the File Explorer opens the file in the editor
        def open_from_explorer():
            explorer = self.open_explorer()
            self.pointer.move_to(explorer_entry(explorer, EDITOR_NOTE_ROW))
            self.pointer.double_click()
        pid, window = self.start_editor(open_from_explorer,
                                        "the File Explorer")
        if pid is None:
            return
        self.check("[UTEXTEDIT] Spawned PID=%s for %s" % (pid, EDITOR_FILE)
                   in self.log_text(),
                   "editor: the File Explorer passed %s" % EDITOR_FILE)
        self.monitor.cmd("sendkey end")
        self.type_text(" +more")
        self.monitor.cmd("sendkey ctrl-s")
        time.sleep(1.0)
        since = len(self.log_text())
        self.monitor.cmd("sendkey esc")
        if not self.editor_ended(pid, "after Ctrl+S, Esc closes without "
                                 "asking", since):
            return

        # N discards the changes
        pid, window = self.editor_from_menu()
        if pid is None:
            return
        box = window.console_rows(*EDITOR_BOX_ROWS)
        self.type_text("scratch")
        time.sleep(1.0)
        before = self.screendump("editor-scratch").logical_area(box)
        self.pointer.home()
        self.pointer.move_to(window.close_button())
        self.pointer.click()
        self.editor_box("editor-discard", pid, window, before, True,
                        "the close button asks about the scratch text")
        since = len(self.log_text())
        self.monitor.cmd("sendkey n")
        self.editor_ended(pid, "N discards the text and closes", since)

        # a program cannot be saved over
        pid, window = self.editor_from_menu()
        if pid is None:
            return
        status = window.console_rows(*EDITOR_STATUS_ROWS)
        self.type_text("x")
        self.monitor.cmd("sendkey ctrl-shift-s")
        time.sleep(0.5)
        self.monitor.cmd("sendkey backspace")
        self.monitor.cmd("sendkey backspace")
        self.type_text("/UTERM.ELF")
        self.monitor.cmd("sendkey ret")

        def no_error(image):
            if self.editor_exited(pid):
                return "UTEXTEDIT.ELF (PID=%s) has exited" % pid
            red = reddish_pixels(image.logical_area(status))
            return None if red > 20 else "no error in the status line"

        issue, seconds = self.watch_screen("editor-readonly", no_error,
                                           WINDOW_TIMEOUT)
        self.check(issue is None, "editor: saving as UTERM.ELF shows an error",
                   "editor-readonly.png, %s" % (issue or "%.1f s" % seconds))
        self.editor_running(pid, "the refused save keeps the document open")
        since = len(self.log_text())
        self.monitor.cmd("sendkey esc")
        time.sleep(0.5)
        self.monitor.cmd("sendkey n")
        self.editor_ended(pid, "then Esc and N close it", since)
        self.editor_features()
        self.check(True, "editor: done", "%.0f s" % (time.monotonic() - start))

    def editor_features(self):
        """Selection, clipboard, undo, find and the mouse, then a file of
        about 100 KB; check_editor_files reads both files on the host."""
        pid, window = self.editor_from_menu()
        if pid is None:
            return

        def cell(row, col):
            return (window.x + 13 + 8 * col, window.y + 42 + 15 * row)

        keys = ["ctrl-a", "ctrl-c", "ctrl-end", "ret", "ctrl-v",  # copy all
                None, "ctrl-z", "ctrl-y", "ctrl-z",                # undo XYZ
                "ctrl-f", None, "ret", None,                       # find beta
                "ctrl-g", "delete"]                                # the next one
        texts = iter(["XYZ", "beta", "BETA"])
        self.type_text("alpha beta gamma\nsecond line")
        for key in keys:
            if key is None:
                self.type_text(next(texts))
            else:
                self.monitor.cmd("sendkey %s" % key)
            time.sleep(0.1)

        # the mouse: a drag over "second" on the second row selects it
        body = window.console_rows(2, 6)
        self.pointer.home()
        self.pointer.move_to(cell(3, 2))
        self.pointer.press()
        self.pointer.move_to(cell(3, 8))
        self.pointer.release()

        def selected(image):
            area = image.logical_area(body)
            count = sum(1 for i in range(0, len(area), 3)
                        if tuple(area[i:i + 3]) == EDITOR_SELECTED_RGB)
            return None if count > 50 else "no selected cells (%d)" % count

        issue, seconds = self.watch_screen("editor-drag", selected, WINDOW_TIMEOUT)
        self.check(issue is None, "editor: a mouse drag selects text",
                   "editor-drag.png, %s" % (issue or "%.1f s" % seconds))
        self.type_text("2nd")
        self.monitor.cmd("sendkey ctrl-s")
        time.sleep(0.5)
        self.type_text("features")
        self.monitor.cmd("sendkey ret")
        time.sleep(1.0)

        # a file over the old 8 KB limit
        self.monitor.cmd("sendkey ctrl-o")  # offers the document's own path
        time.sleep(0.5)
        for _ in EDITOR_FEATURES_FILE:
            self.monitor.cmd("sendkey backspace")
            time.sleep(0.02)
        self.type_text(EDITOR_BIG_FILE + "\n")
        time.sleep(1.5)
        self.monitor.cmd("sendkey ctrl-end")
        self.type_text("END")
        self.monitor.cmd("sendkey ctrl-s")
        time.sleep(1.5)
        since = len(self.log_text())
        self.monitor.cmd("sendkey esc")
        self.editor_ended(pid, "the features document and the large file "
                          "are saved: Esc closes without asking", since)

    def check_editor_files(self):
        """What the editor test left on the disk, read on the host."""
        result = mkgemfs("cat", self.disks["data"], EDITOR_FILE)
        self.check(result.returncode == 0 and result.stdout == EDITOR_SAVED,
                   "disk: %s holds the text typed, saved, reopened and saved "
                   "again" % EDITOR_FILE, repr(result.stdout[:60]))
        uterm = os.path.join(os.path.dirname(self.args.image),
                             "uterm_image.bin")
        with open(uterm, "rb") as f:
            expected = f.read()
        result = mkgemfs("cat", self.disks["data"], "/UTERM.ELF")
        self.check(result.returncode == 0 and result.stdout == expected,
                   "disk: UTERM.ELF is still the program from the kernel")
        result = mkgemfs("ls", self.disks["data"], EXPLORER_HOME)
        names = {line.split(maxsplit=3)[3] for line in result.stdout.decode().splitlines()}
        self.check(result.returncode == 0 and
                   names == {"notes.txt", "features.txt", "Projects/"},
                   "disk: Home contains the folder and the documents",
                   ", ".join(sorted(names)))
        for path, saved in [(EDITOR_FEATURES_FILE, EDITOR_FEATURES_SAVED),
                            (EDITOR_BIG_FILE, EDITOR_BIG_TEXT + b"END")]:
            result = mkgemfs("cat", self.disks["data"], path)
            self.check(result.returncode == 0 and result.stdout == saved,
                       "disk: %s holds what the editor made of it" % path,
                       "%d bytes, ends %r" % (len(result.stdout), result.stdout[-30:]))

    def explorer_scroll(self):
        """Browse a full directory with the keyboard and the scrollbar."""
        window = self.open_explorer()
        self.pointer.move_to(explorer_entry(window, 0))
        self.pointer.double_click()
        self.pointer.move_to((SCREEN_SIZE[0] // UI_SCALE - 20, 45))

        y = explorer_list_y(window)
        # Client height is window height minus 28 px. Toolbar, column header
        # and footer overhead are 32, 25 and 24 px respectively.
        rows = (window.h - 28 - 32 - 25 - 24) // EXPLORER_ROW_HEIGHT
        bottom = y + rows * EXPLORER_ROW_HEIGHT
        rail = window.x + window.w - 8
        first_name = (window.x + 38, y + 2, window.x + 208, y + 21)

        def top_thumb(image):
            if image.logical_rgb((rail, y + 6)) != FRAME_RGB:
                return "the directory has not appeared"
            return None

        issue, _ = self.watch_screen("explorer-large-folder", top_thumb,
                                     self.args.app_timeout)
        if not self.check(issue is None, "explorer: the large folder has a scrollbar",
                          issue or ""):
            return
        image = self.screendump("explorer-large-folder", save=False)
        column_x = window.x + window.w - 127
        first_type = image.logical_area((column_x, y + 2, rail - 10, y + 21))
        second_type = image.logical_area((column_x, y + 26, rail - 10, y + 45))
        self.check(first_type == second_type,
                   "explorer: a long name stays inside the Name column")

        baseline = image.logical_area(first_name)
        self.monitor.cmd("sendkey pgdn")
        issue, _ = self.watch_screen(
            "explorer-page-down",
            lambda image: None if image.logical_area(first_name) != baseline
            and image.logical_rgb((rail, y + 6)) != FRAME_RGB
            else "Page Down has not advanced to the next page",
            self.args.app_timeout,
        )
        self.check(issue is None, "explorer: Page Down advances the list", issue or "")
        self.monitor.cmd("sendkey home")
        issue, _ = self.watch_screen("explorer-home", top_thumb,
                                     self.args.app_timeout)
        if not self.check(issue is None, "explorer: Home returns to the first row",
                          issue or ""):
            return

        self.pointer.move_to((rail, y + 6))
        self.pointer.press()
        self.pointer.move_to((rail, bottom - 2))
        self.pointer.release()
        icon = (window.x + 11, bottom - 21, window.x + 29, bottom - 4)
        issue, _ = self.watch_screen(
            "explorer-last-page",
            lambda image: None if image.logical_rgb((rail + 1, bottom - 12)) == FRAME_RGB
            and bytes(INK_RGB) in image.logical_area(icon)
            else "the last page has not appeared",
            self.args.app_timeout,
        )
        if not self.check(issue is None, "explorer: dragging the thumb reaches the last page",
                          issue or ""):
            return

        def open_last():
            self.pointer.move_to(explorer_entry(window, rows - 1))
            self.pointer.double_click()

        pid, _ = self.start_editor(open_last, "the last row")
        if pid is not None:
            self.check("[UTEXTEDIT] Spawned PID=%s for %s" % (pid, EXPLORER_LAST_FILE)
                       in self.log_text(), "explorer: the last of 110 files opens")
            since = len(self.log_text())
            self.monitor.cmd("sendkey esc")
            self.editor_ended(pid, "the last file closes", since)

    # -- Explorer name operations --------------------------------------------
    def explorer_screen(self, shot, what, probe):
        issue, _ = self.watch_screen(shot, lambda image: None if probe(image)
                                     else what, self.args.app_timeout)
        if not self.check(issue is None, "explorer: " + what, issue or shot + ".png"):
            raise SmokeError(what)

    def explorer_name_screen(self, window, shot, what, editing=True,
                             error=False, prefilled=False):
        # Toolbar name field and the error line on the footer.
        field = (window.x + 98, window.y + 31)
        footer = (window.x + 1, window.y + window.h - 18)
        selection = (field[0] + 6, field[1] + 3)

        def probe(image):
            return (image.logical_rgb(field) == (ACCENT_RGB if editing else PAPER_RGB)
                    and (image.logical_rgb(footer) == ERROR_RGB) == error
                    and (not prefilled or image.logical_rgb(selection) == SELECTION_RGB))

        self.explorer_screen(shot, what, probe)

    def explorer_new_folder(self, window):
        self.explorer_context_action(explorer_entry(window, 3), 2, 0, "context-new-folder")

    def explorer_select_row(self, window, row):
        self.pointer.move_to(explorer_entry(window, row))
        self.pointer.click()

    def explorer_context(self, point, items, shot):
        context_re = re.compile(r"\[MENU\] Context " + Window.GEOMETRY)
        since = len(self.log_text())
        self.pointer.move_to(point)
        self.pointer.click(2)
        match = self.wait_for(lambda text: context_re.search(text, since),
                              self.args.app_timeout)
        if not match:
            raise SmokeError("right click did not open the system menu")
        menu = Window.parse(match)
        width, height = (size // UI_SCALE for size in SCREEN_SIZE)
        area = Window(0, 28, width, height - 28 - DOCK_HEIGHT)
        if not self.check(menu.inside(area) and menu.h == 2 + 24 * items,
                          "explorer: the system menu fits and offers the expected rows"):
            raise SmokeError("context menu geometry is wrong")
        self.explorer_screen(shot, "the context menu is drawn", lambda image:
                             image.logical_rgb((menu.x + 24, menu.y)) == FRAME_RGB)
        return menu

    def explorer_context_action(self, point, items, row, shot):
        menu = self.explorer_context(point, items, shot)
        self.pointer.move_to((menu.x + 40, menu.y + 13 + 24 * row))
        self.pointer.click()

    def explorer_enter_work(self, window):
        # Home's first entry was seeded as Work. A rename keeps its slot.
        self.explorer_select_row(window, 0)
        self.monitor.cmd("sendkey ret")
        self.wait_explorer_path(RENAME_DIR)
        self.check(True, "explorer: the renamed folder opens")

    def explorer_names(self):
        window = self.open_explorer()
        log = self.log_text()
        explorer_pid = re.search(r"\[PROC\] Spawned PID=(\d+) EXPLORER\.ELF\b", log)
        self.check(explorer_pid is not None
                   and "[WINDOW] Opened PID=%s " % explorer_pid.group(1) in log,
                   "explorer: EXPLORER.ELF owns its window in Ring 3")
        path_end = (window.x + window.w - 53, window.y + 31)
        self.explorer_screen("home-toolbar", "the path has room across the toolbar",
                             lambda image: image.logical_rgb(path_end) == PAPER_RGB)
        blank = explorer_entry(window, 3)
        menu = self.explorer_context(blank, 2, "context-empty")
        since = len(self.log_text())
        self.explorer_up(window)
        self.explorer_screen("context-dismissed", "an outside click only dismisses the menu",
                             lambda image: image.logical_rgb((menu.x + 24, menu.y)) == PAPER_RGB)
        opened_root = re.compile(r"\[EXPLORER\] Opened /\r?\n")
        navigated = self.wait_for(lambda text: opened_root.search(text, since), 0.75)
        self.check(not navigated, "explorer: the dismissed menu leaves Home open")
        self.explorer_new_folder(window)
        self.explorer_name_screen(window, "new-folder", "right-click New Folder shows the input")
        self.type_text("cancelled")
        self.monitor.cmd("sendkey esc")
        self.explorer_name_screen(window, "cancel-create", "Escape cancels creation",
                                  editing=False)

        self.explorer_new_folder(window)
        self.monitor.cmd("sendkey ret")
        self.explorer_name_screen(window, "empty-name", "an empty name is rejected", error=True)
        self.type_text("bad/name")
        self.explorer_name_screen(window, "edit-invalid", "editing clears the error")
        self.monitor.cmd("sendkey ret")
        self.explorer_name_screen(window, "invalid-name", "a slash is rejected", error=True)
        self.monitor.cmd("sendkey ctrl-a")
        self.type_text("w" * 56)
        self.explorer_name_screen(window, "edit-long", "the long name can be edited")
        self.monitor.cmd("sendkey ret")
        self.explorer_name_screen(window, "long-name", "56 letters are rejected", error=True)
        self.monitor.cmd("sendkey ctrl-a")
        self.type_text("Work")
        self.explorer_name_screen(window, "edit-taken", "the same field accepts a correction")
        self.monitor.cmd("sendkey ret")
        self.explorer_name_screen(window, "existing-folder", "an existing folder name is rejected",
                                  error=True)
        self.monitor.cmd("sendkey ctrl-a")
        self.type_text("New Notes")
        self.pointer.move_to((window.x + window.w - 96, window.y + 42))
        self.pointer.click()  # OK also submits the same prompt.
        self.explorer_name_screen(window, "created-folder", "a named folder is created",
                                  editing=False)
        _, y = explorer_entry(window, 1)  # Home initially contained only Work.
        icon = (window.x + 11, y - 8, window.x + 29, y + 8)
        self.explorer_screen("created-row", "the new folder appears without input",
                             lambda image: bytes(INK_RGB) in image.logical_area(icon))

        self.explorer_context_action(explorer_entry(window, 0), 4, 1, "context-folder")
        self.explorer_name_screen(window, "rename-folder", "right-click Rename prefills the folder name",
                                  prefilled=True)
        self.type_text(RENAME_FOLDER)
        self.monitor.cmd("sendkey ret")
        self.explorer_name_screen(window, "renamed-folder", "Enter renames the folder",
                                  editing=False)
        y = explorer_list_y(window)
        first_icon = (window.x + 11, y + 4, window.x + 29, y + 20)
        self.explorer_screen("renamed-row", "the renamed folder appears without input",
                             lambda image: bytes(INK_RGB) in image.logical_area(first_icon))
        self.explorer_enter_work(window)
        self.explorer_context_action(explorer_entry(window, 0), 4, 1, "context-file")
        self.explorer_name_screen(window, "rename-file", "right-click Rename prefills the file name",
                                  prefilled=True)
        self.type_text("taken.txt\n")
        self.explorer_name_screen(window, "rename-collision", "a rename cannot replace another file",
                                  error=True)
        self.monitor.cmd("sendkey ctrl-a")
        self.type_text("xinal notes.txt")
        self.monitor.cmd("sendkey home")
        self.monitor.cmd("sendkey delete")
        self.type_text("f\n")
        self.explorer_name_screen(window, "renamed-file", "the corrected file name is saved",
                                  editing=False)
        self.explorer_file_operations(window)

    def explorer_logged(self, what, path, timeout=None):
        line = re.compile(r"\[EXPLORER\] %s %s\r?\n" % (what, re.escape(path)))
        return self.wait_for(lambda text: line.search(text),
                             self.args.app_timeout if timeout is None else timeout)

    def explorer_confirm_screen(self, window, shot, what):
        toolbar = (window.x + 2, window.y + 30, window.x + window.w - 2, window.y + 50)
        self.explorer_screen(shot, what, lambda image:
                             reddish_pixels(image.logical_area(toolbar)) > 20)

    def explorer_file_operations(self, window):
        """Copy and Paste with the keyboard and the menu, Delete with its
        question (Esc cancels, Enter and the Delete button confirm), and a
        new text file that opens in the editor."""
        # in My Work: final notes.txt (row 0), taken.txt (row 1)
        self.explorer_select_row(window, 0)
        self.monitor.cmd("sendkey ctrl-c")
        self.monitor.cmd("sendkey ctrl-v")
        copy = RENAME_DIR + "/final notes copy.txt"
        self.check(bool(self.explorer_logged("Pasted", copy)),
                   "explorer: Ctrl+C, Ctrl+V pastes a copy beside the file")

        self.explorer_select_row(window, 2)
        self.monitor.cmd("sendkey delete")
        self.explorer_confirm_screen(window, "delete-question", "Delete asks first")
        self.monitor.cmd("sendkey esc")
        self.check(not self.explorer_logged("Deleted", copy, timeout=0.75),
                   "explorer: Escape keeps the file")
        self.monitor.cmd("sendkey delete")
        self.explorer_confirm_screen(window, "delete-again", "Delete asks again")
        self.monitor.cmd("sendkey ret")
        self.check(bool(self.explorer_logged("Deleted", copy)),
                   "explorer: Enter deletes the file")

        # Home: My Work (row 0), New Notes (row 1)
        self.explorer_up(window)
        self.wait_explorer_path(EXPLORER_HOME)
        self.explorer_context_action(explorer_entry(window, 0), 4, 2, "context-copy")
        self.explorer_context_action(explorer_entry(window, 3), 3, 2, "context-paste")
        folder_copy = EXPLORER_HOME + "/My Work copy"
        self.check(bool(self.explorer_logged("Pasted", folder_copy)),
                   "explorer: the menu pastes a copy of the folder")
        self.explorer_context_action(explorer_entry(window, 2), 4, 3, "context-delete")
        self.explorer_confirm_screen(window, "delete-folder", "deleting a folder asks first")
        self.pointer.move_to((window.x + window.w - 130, window.y + 42))
        self.pointer.click()  # the Delete button
        self.check(bool(self.explorer_logged("Deleted", folder_copy)),
                   "explorer: the Delete button deletes the folder and its files")

        start_re = re.compile(r"\[UTEXTEDIT\] Spawned PID=(\d+) for " +
                              re.escape(NEW_TEXT_FILE))
        since = len(self.log_text())
        self.explorer_context_action(explorer_entry(window, 3), 3, 1, "context-new-text")
        self.explorer_name_screen(window, "new-text", "New Text File suggests a name",
                                  prefilled=True)
        self.monitor.cmd("sendkey ret")
        started = self.wait_for(lambda text: start_re.search(text, since),
                                self.args.app_timeout)
        if not self.check(bool(started) and bool(self.explorer_logged("Created", NEW_TEXT_FILE)),
                          "explorer: the new text file opens in the editor"):
            raise SmokeError("the new text file did not open")
        pid = started.group(1)
        self.monitor.cmd("sendkey esc")  # nothing typed: the editor just closes
        if not self.wait_for(lambda text: "[PROC] Reaped PID=%s exit=0" % pid in text,
                             self.args.app_timeout):
            raise SmokeError("the editor of the new file did not close")

    def explorer_names_reboot(self):
        self.check("[PROC] Programs on GemFS: 0 seeded, 4 up to date" in self.log_text(),
                   "explorer: reboot kept the built-in programs")
        window = self.open_explorer()
        self.explorer_enter_work(window)
        start_re = re.compile(r"\[UTEXTEDIT\] Spawned PID=\d+ for " + re.escape(RENAME_FILE))
        since = len(self.log_text())
        self.explorer_select_row(window, 0)
        time.sleep(1.0)
        self.explorer_select_row(window, 0)
        self.check(not self.wait_for(lambda text: start_re.search(text, since), 0.75),
                   "explorer: two separate clicks only select the file")

        def open_file():
            self.pointer.move_to(explorer_entry(window, 0))
            self.pointer.double_click()

        pid, _ = self.start_editor(open_file, "renamed file after reboot")
        if pid is not None:
            self.check("[UTEXTEDIT] Spawned PID=%s for %s" % (pid, RENAME_FILE)
                       in self.log_text(), "explorer: the editor gets the renamed path")
            since = len(self.log_text())
            self.monitor.cmd("sendkey esc")
            self.editor_ended(pid, "the renamed file closes", since)

    def check_explorer_names(self):
        disk = self.disks["data"]
        for path, entries in [("/", {"Home/", "ABOUT.ELF", "UTERM.ELF", "USRSMOKE.ELF", "UTEXTEDIT.ELF"}),
                              (EXPLORER_HOME, {"My Work/", "New Notes/", "New Text.txt"}),
                              (RENAME_DIR, {"final notes.txt", "taken.txt"}),
                              (EXPLORER_HOME + "/New Notes", set())]:
            result = mkgemfs("ls", disk, path)
            names = {line.split(maxsplit=3)[3]
                     for line in result.stdout.decode().splitlines() if line}
            self.check(result.returncode == 0 and names == entries,
                       "disk: expected names in " + path, ", ".join(sorted(names)))
        for path, saved in [(RENAME_FILE, RENAME_FILES["draft.txt"]),
                            (RENAME_DIR + "/taken.txt", RENAME_FILES["taken.txt"]),
                            (NEW_TEXT_FILE, b"")]:
            result = mkgemfs("cat", disk, path)
            self.check(result.returncode == 0 and result.stdout == saved,
                       "disk: content preserved in " + path)

    # -- self-test -------------------------------------------------------------
    def selftest(self, boot):
        """One boot of the self-test image. Check names of the second boot
        say so; the first boot writes files that the second one checks."""
        label = "selftest" if boot == 1 else "selftest (second boot)"
        start = time.monotonic()
        result_re = re.compile(r"^\[SELFTEST\] RESULT: (PASS|FAIL) \("
                               r"(?:\d+ of )?(\d+) checks(?:, (\d+) skipped)?"
                               r"\)$", re.M)
        panic_re = re.compile(r"^\[PANIC\][^\n]*(?=\n)", re.M)  # whole line

        def finished(text):
            # a panic before the result stops the kernel: no need to wait
            text = text.replace("\r", "")
            return result_re.search(text) or panic_re.search(text)

        result = self.wait_for(finished, SELFTEST_TIMEOUT)
        if result and result.re is panic_re:
            self.check(False, label + ": finished",
                       "kernel panic before the result: %s" % result.group(0))
            return
        if not self.check(bool(result), label + ": finished",
                          "%.1f s" % (time.monotonic() - start)):
            return
        text = self.log_text().replace("\r", "")
        passed = re.findall(r"^\[SELFTEST\] PASS (.*)$", text, re.M)
        failed = re.findall(r"^\[SELFTEST\] FAIL (.*)$", text, re.M)
        skipped = re.findall(r"^\[SELFTEST\] SKIP (.*)$", text, re.M)
        total = int(result.group(2))
        self.check(result.group(1) == "PASS" and not failed and
                   len(passed) == total,
                   "%s: all %d checks passed" % (label, total),
                   "; ".join(failed[:3]) if failed else
                   "%d PASS lines" % len(passed))
        explorer_checks = {
            "explorer: GUI runs while a folder read waits for a write",
            "explorer: shows the current folder after the write",
            "explorer: a Rust panic ends only its process",
            "explorer: the desktop reopens and closes Explorer after a panic",
        }
        self.check(explorer_checks.issubset(passed),
                   label + ": Explorer concurrency and panic recovery checks passed")
        if boot == 1:
            self.check(any(line.startswith("fs: wrote " + PERSIST_NOTE)
                           for line in passed),
                       label + ": wrote a file for the second boot")
        else:
            survived = [line for line in passed
                        if line.startswith("fs: after a reboot")]
            self.check(len(survived) == 2,
                       label + ": the files of the first boot survived",
                       "; ".join(survived) or "no 'after a reboot' checks")
            self.check(re.search(r"^\[PROC\] Programs on GemFS: 0 seeded",
                                 text, re.M) is not None,
                       label + ": the programs were not written again")
            # failed, interrupted and killed writes gave their blocks back
            self.check("[GemFS] Nothing to reclaim" in text,
                       label + ": the first boot left nothing to reclaim")
        for line in skipped:
            print("SKIP  %s: %s" % (label, line), flush=True)

        # every fault test ended its program, and nothing else faulted
        faults = [line for line in passed if line.startswith("fault ")]
        user_faults = re.findall(r"^\[USERFAULT\] ", text, re.M)
        self.check(len(faults) > 0 and len(user_faults) == len(faults),
                   label + ": one [USERFAULT] per fault test",
                   "%d fault tests, %d [USERFAULT] lines"
                   % (len(faults), len(user_faults)))
        self.screendump("selftest" if boot == 1 else "selftest-boot2")

        overflow = self.wait_for(lambda t: SELFTEST_OVERFLOW in t,
                                 SELFTEST_PANIC_TIMEOUT)
        if not self.check(bool(overflow),
                          label + ": kernel stack overflow test started"):
            return
        text = self.log_text().replace("\r", "")
        before = text[:text.index(SELFTEST_OVERFLOW)]
        self.check("PANIC" not in before,
                   label + ": no panic before the overflow test")
        bad = interleaved_lines(before)
        self.check(not bad, label + ": no interleaved lines",
                   "; ".join(repr(line) for line in bad[:3]))

        panic = self.wait_for(
            lambda t: SELFTEST_DOUBLE_FAULT.search(t.replace("\r", "")),
            SELFTEST_PANIC_TIMEOUT)
        text = self.log_text()
        self.check(bool(panic) and text.count("[PANIC]") == 1,
                   label + ": the overflow ends in the double fault handler",
                   "guard page named, registers dumped" if panic else
                   "no double fault panic naming the guard page")
        self.check(self.qemu.poll() is None, "QEMU still running at the end")

    def check_between_boots(self):
        """What the first self-test boot left on the GemFS disk, read on the
        host with tools/mkgemfs."""
        self.check_gemfs("after the first boot")
        for path, expected, what in (
                (PERSIST_NOTE, PERSIST_TEXT, "from the kernel"),
                (FILETEST_BIG, FILETEST_TABLE, "from FILETEST.ELF")):
            result = mkgemfs("cat", self.disks["data"], path)
            self.check(result.returncode == 0 and result.stdout == expected,
                       "disk: %s (%s) reads back on the host" % (path, what),
                       "%d bytes" % len(result.stdout) if result.returncode == 0
                       else result.stderr.decode(errors="replace").strip())

    def scan_log(self):
        text = self.log_text()
        hits = []
        for pattern in FAILURE_PATTERNS:
            for match in re.finditer(pattern, text):
                start = text.rfind("\n", 0, match.start()) + 1
                end = text.find("\n", match.end())
                hits.append(text[start:end if end >= 0 else None].strip())
        self.check(not hits, "serial log: no PANIC, faults or failures",
                   "; ".join(hits[:3]))
        bad = interleaved_lines(text)
        self.check(not bad, "serial log: no interleaved lines",
                   "; ".join(repr(line) for line in bad[:3]))
        if self.args.data_disk == "gemfs" and not self.args.no_data_disk:
            # the kernel walks the tree at mount; only --leftovers left
            # anything for it to free
            line = (LEFTOVERS_RECLAIMED if self.args.leftovers else
                    "[GemFS] Nothing to reclaim")
            self.check(line in text, "serial log: %s" % line)
        for pattern in self.args.expect_log:
            self.check(re.search(pattern, text) is not None,
                       "serial log: /%s/" % pattern)
        self.check(self.qemu.poll() is None, "QEMU still running at the end")

    def shutdown(self):
        """GemOS -> Shutdown: the kernel lets a GemFS call in progress
        finish, keeps any other from starting and turns the machine off,
        so QEMU exits by itself. With --no-acpi nothing turns it off: the
        kernel halts, and the screen says it is safe to turn it off."""
        since = len(self.log_text())
        self.pointer.home()
        self.pointer.move_to(MENU_GEMOS)
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(menu_item(60, 1))  # GemOS -> Shutdown
        start = time.monotonic()
        try:
            # the menu acts on the press; the button is never released
            self.monitor.cmd("mouse_button 1")
        except (OSError, SmokeError):
            pass  # QEMU has already turned off

        if self.args.no_acpi:
            halted = self.wait_for(
                lambda t: "[POWER] Still running" in t[since:], 30.0)
            self.check(bool(halted),
                       "shutdown: without ACPI the kernel halts, QEMU stays "
                       "on", "%.1f s" % (time.monotonic() - start))

            def problem(image):
                width, height = (v // UI_SCALE for v in SCREEN_SIZE)
                corner = image.logical_rgb((10, 10))
                middle = image.logical_area((width // 2 - 150, height // 2 - 12,
                                             width // 2 + 150, height // 2 + 12))
                white = sum(1 for i in range(0, len(middle), 3)
                            if min(middle[i:i + 3]) > 200)
                if corner != (0, 0, 0):
                    return "corner pixel #%02x%02x%02x" % corner
                if white < 200:
                    return "%d white pixels in the middle" % white
                return None

            issue, seconds = self.watch_screen("shutdown", problem,
                                               WINDOW_TIMEOUT)
            self.check(issue is None, "shutdown: the screen says it is safe "
                       "to turn the computer off",
                       "shutdown.png, %s" % (issue or "%.1f s" % seconds))
            return

        try:
            code = self.qemu.wait(timeout=30)
        except subprocess.TimeoutExpired:
            code = None
        self.check(code == 0, "shutdown: GemOS -> Shutdown turns the machine "
                   "off", "QEMU exited with status %s after %.1f s"
                   % (code, time.monotonic() - start) if code is not None
                   else "QEMU still running after 30 s")
        text = self.log_text()
        steps = ["[POWER] Shutdown requested", "[POWER] Powering off"]
        if "[GemFS] Mounted" in text:
            steps.insert(1, "[GemFS] Stopped")
        found = [text.find(step, since) for step in steps]
        failures = [p for p in FAILURE_PATTERNS + [r"\[POWER\] Still running"]
                    if re.search(p, text[since:])]
        self.check(min(found) >= 0 and found == sorted(found) and not failures,
                   "shutdown: the log shows %s, in that order"
                   % ", then ".join(steps),
                   "missing: %s" % ", ".join(s for s, f in zip(steps, found)
                                             if f < 0) if min(found) < 0
                   else "failures: %s" % ", ".join(failures) if failures
                   else "")

    def save_diagnostics(self):
        """On failure: the screen and the CPU registers (EIP shows where the
        guest is, e.g. `nm build/kernel.elf | sort`) next to the log."""
        try:
            self.screendump("failure")
            with open(os.path.join(self.out, "registers.txt"), "wb") as f:
                f.write(self.monitor.cmd("info registers"))
            print("diagnostics: failure.png, registers.txt", flush=True)
        except (OSError, SmokeError, ValueError):
            pass

    def run(self):
        self.prepare_disks()
        if self.args.editor:
            self.prepare_explorer_directory()
        if self.args.explorer:
            self.prepare_explorer_names()
        if self.args.preboot:
            self.preboot()
        if self.args.leftovers:
            self.crash_leftovers()
        self.snapshot_disks()
        boots = 2 if self.args.selftest or self.args.explorer else 1
        reboot_hash = None
        for boot in range(1, boots + 1):
            if boot == 2:
                if self.args.explorer:
                    self.check_gemfs("before restarting")
                    reboot_hash = sha256(self.disks["data"])
                else:
                    self.check_between_boots()
            if not self.run_once(boot):
                return
        self.check_disks()
        if self.args.explorer:
            self.check_explorer_names()
            self.check(sha256(self.disks["data"]) == reboot_hash,
                       "disk: reboot and reopening the file made no writes")

    def run_once(self, boot):
        """One QEMU run of the test; False if it could not finish."""
        self.start_qemu("serial.log" if boot == 1 else "serial-boot2.log")
        try:
            if not self.boot():
                self.save_diagnostics()
                return False
            self.desktop()
            if not (self.args.selftest or self.args.stress or
                    self.args.editor or self.args.explorer):
                self.cursor_frames()
                self.clock()
                self.menu_hover_frames()
                self.log_viewer()
            if self.args.selftest:
                self.selftest(boot)
            elif self.args.stress:
                self.stress()
            elif self.args.editor:
                self.editor()
                self.explorer_scroll()
            elif self.args.explorer:
                if boot == 1:
                    self.explorer_names()
                else:
                    self.explorer_names_reboot()
            else:
                for name, menu, item, updates_itself in APPS:
                    self.run_app(name, menu, item, updates_itself)
                    time.sleep(0.5)
                self.partial_frames()
                self.windows()
                self.console_grid()
                self.file_explorer()
            if not self.args.selftest:
                self.scan_log()
                self.shutdown()
            if any(not ok for ok, _, _ in self.results):
                self.save_diagnostics()
        except SmokeError:
            self.save_diagnostics()
            raise
        finally:
            self.stop_qemu()
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", default="build/gemos.img")
    parser.add_argument("--out", default="build/smoke")
    parser.add_argument("--timeout", type=float, default=420.0,
                        help="overall time budget in seconds")
    parser.add_argument("--boot-timeout", type=float, default=120.0)
    parser.add_argument("--app-timeout", type=float, default=45.0)
    parser.add_argument("--stress", type=int, default=0, metavar="CYCLES",
                        help="run the load test with this many cycles")
    parser.add_argument("--matrix", action="store_true",
                        help="run the smoke test on every machine variant")
    parser.add_argument("--selftest", action="store_true",
                        help="check the report of the self-test image "
                        "(--image build/selftest/gemos.img)")
    parser.add_argument("--boot", choices=("floppy", "hdd", "cdrom"),
                        default="floppy",
                        help="boot device for --image")
    parser.add_argument("--memory", type=int, default=128, metavar="MB")
    parser.add_argument("--no-data-disk", action="store_true")
    parser.add_argument("--data-disk", choices=("gemfs", "unsigned", "damaged"),
                        default="gemfs",
                        help="a fresh GemFS disk, random bytes, or a GemFS "
                        "disk with a wrong superblock checksum")
    parser.add_argument("--slow-writes", type=int, default=0, metavar="N",
                        help="let QEMU write at most N requests per second "
                        "to the data disk")
    parser.add_argument("--editor", action="store_true",
                        help="open, edit and save files in UTEXTEDIT.ELF")
    parser.add_argument("--explorer", action="store_true",
                        help="create and rename entries, then reopen after a reboot")
    parser.add_argument("--preboot", action="store_true",
                        help="boot once on the new disks before the test")
    parser.add_argument("--leftovers", action="store_true",
                        help="after --preboot, leave on the data disk what "
                        "a crash can leave; the kernel must free it")
    parser.add_argument("--expect-unchanged", action="append", default=[],
                        choices=("data", "boot"),
                        help="that disk image must be the same afterwards")
    parser.add_argument("--no-acpi", action="store_true",
                        help="a machine without ACPI: Shutdown cannot turn "
                        "it off")
    parser.add_argument("--vga-mem", type=int, default=0, metavar="MB",
                        help="VRAM of the QEMU VGA (default: QEMU's 16)")
    parser.add_argument("--cpu", help="QEMU CPU model and feature flags")
    parser.add_argument("--resolution", default="1920x1080",
                        help="screen size the kernel should pick")
    parser.add_argument("--expect-log", action="append", default=[],
                        metavar="REGEX", help="must match the serial log")
    args = parser.parse_args()
    if args.matrix:
        return run_matrix(args)
    set_resolution(*(int(v) for v in args.resolution.split("x")))

    def on_signal(signum, _frame):
        raise SmokeError("interrupted by signal %d" % signum)

    signal.signal(signal.SIGTERM, on_signal)
    signal.signal(signal.SIGINT, on_signal)

    smoke = Smoke(args)
    error = None
    try:
        smoke.run()
    except SmokeError as exc:
        error = str(exc)
    except OSError as exc:
        error = "%s: %s" % (type(exc).__name__, exc)

    failed = [r for r in smoke.results if not r[0]]
    if error:
        print("ERROR  %s" % error, flush=True)
    if error or failed or not smoke.results:
        tail = smoke.log_text().splitlines()[-40:]
        if tail:
            print("---- last serial log lines ----")
            print("\n".join(tail))
            print("-------------------------------")
        print("SMOKE: FAIL (%d of %d checks failed%s); artifacts in %s" % (
            len(failed), len(smoke.results), ", " + error if error else "",
            smoke.out), flush=True)
        return 2 if error and not smoke.results else 1
    print("SMOKE: PASS (%d checks); artifacts in %s" % (
        len(smoke.results), smoke.out), flush=True)
    return 0


def run_matrix(args):
    """Run the smoke test once per MATRIX variant in its own QEMU."""
    here = os.path.abspath(__file__)
    images = {"floppy": args.image,
              "hdd": os.path.join(os.path.dirname(args.image),
                                  "gemos-hdd.img")}
    failed = []
    deadline = time.monotonic() + args.timeout
    for index, (name, extra, logs) in enumerate(MATRIX):
        boot = extra[extra.index("--boot") + 1] if "--boot" in extra \
            else "floppy"
        # what is left, shared by the variants still to run: the quick
        # ones leave time for the slow disk
        budget = (deadline - time.monotonic()) / (len(MATRIX) - index)
        cmd = [sys.executable, here, "--image", images[boot],
               "--out", os.path.join(args.out, name),
               "--timeout", "%.0f" % max(budget, 1.0)] + extra
        for pattern in logs:
            cmd += ["--expect-log", pattern]
        print("== matrix: %s" % name, flush=True)
        result = subprocess.run(cmd)
        if result.returncode != 0:
            failed.append(name)
    if failed:
        print("MATRIX: FAIL (%s)" % ", ".join(failed), flush=True)
        return 1
    print("MATRIX: PASS (%d variants)" % len(MATRIX), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
