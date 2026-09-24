#!/usr/bin/env python3
"""GemOS QEMU smoke test.

Boots build/gemos.img headless (floppy + fresh IDE data disk, 128 MB, serial
log to a file, HMP monitor on a unix socket) and drives the desktop through
the monitor:

  * the kernel reaches "[BOOT] Window Manager Active",
  * the desktop screendump is a rendered 1920x1080 frame,
  * UTERM.ELF, ABOUT.ELF and UTEXTEDIT.ELF are started from the topbar menus,
    open their console window, close on Esc and are reaped with exit=0,
  * every frame checked must appear on screen by itself: no input is sent
    while waiting for it,
  * the serial log has no PANIC, user fault, allocation failure or refused
    disk write, and no line was printed into the middle of another one,
  * after QEMU has stopped, tools/mkgemfs check finds the GemFS data disk
    consistent.

The data disk is a fresh GemFS disk (tools/mkgemfs format), or with
--data-disk "unsigned" (random bytes) or "damaged" (a GemFS superblock with
a wrong checksum). --preboot boots once on the new disks and shuts down
before the test, which then runs on a second boot. --slow-writes N lets
QEMU write only N requests per second to the data disk. --expect-unchanged
data|boot compares the SHA-256 of that disk image before and after the
test: the kernel must not have written a single byte.

With --matrix it runs the smoke test once per machine variant (32/64/256 MB
of RAM, no data disk, 4 MB of VRAM, boot from the hard disk image, a data
disk without GemFS or with a damaged superblock, a second boot, a slow
disk); each
variant also checks log lines that show which path the kernel took.

With --selftest it boots the self-test image (make selftest) instead, with
a second disk without GemFS: kernel/selftest.c checks the heap, the pool,
the ELF loader, GemFS and the file syscalls, the FPU state and every
exception a Ring 3 program can raise, logs "[SELFTEST] RESULT", then
overflows its kernel stack on purpose; the log must end in the double
fault panic that names the overflow. The self-test boots twice on the same
disks: between the boots the harness checks the GemFS disk and the files
the first boot wrote, and the second boot checks those files again. The
second disk must come out of both boots unchanged.

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
import zlib

# The desktop runs at 1920x1080 with ui_scale 2.0 (kernel/kernel.c), so
# PS/2 mouse deltas and all coordinates below are logical (960x540) pixels.
# Smaller modes use ui_scale 1.0; --resolution changes both.
UI_SCALE = 2
SCREEN_SIZE = (1920, 1080)


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
]

# Topbar menus (kernel/gui/topbar/topbar.c): "GemOS" at x 10..80, "Apps" at
# x 90..160, bar height 28. Menus open at y=28 with 24 px items
# (kernel/ui/menu.h), so item i is centred at y = 29 + 24*i + 12.
MENU_GEMOS = (40, 13)
MENU_APPS = (121, 13)


def menu_item(x, index):
    return (x, 41 + 24 * index)


# ABOUT.ELF redraws its uptime once a second through console_present: a
# redraw requested by a syscall, not by input. Its client area (logical
# x0, y0, x1, y1) must change on its own. The window is 60x18 cells at
# (120, 100) (userland/about/about_theme.h, kernel/console.c).
ABOUT_CLIENT = (124, 128, 612, 408)

# (program, menu to open, item to click, area that updates by itself)
APPS = [
    ("UTERM.ELF", MENU_APPS, menu_item(130, 1), None),    # Apps -> Terminal
    ("ABOUT.ELF", MENU_GEMOS, menu_item(60, 0), ABOUT_CLIENT),  # GemOS -> About
    ("UTEXTEDIT.ELF", MENU_APPS, menu_item(130, 3), None),  # Apps -> Text Editor
]

# Hosted console windows open at logical (120, 100) (kernel/console.c); this
# point lies on the title bar of every hosted app, away from text and buttons.
TITLE_BAR_PROBE = (500, 103)

# Stress test: what to type into each program (QEMU sendkey names; no Esc
# and no "q", which close windows) and where its close button is. A hosted
# window at (120, 100) is cols * 8 + 16 wide; the button is 16 px, 6 px from
# the right edge and 5 px below the top (kernel/gui/wm/wm.c).
STRESS_TYPING = {
    "UTERM.ELF": ["h", "e", "l", "p", "ret", "p", "i", "d", "ret",
                  "t", "i", "c", "k", "s", "ret", "a", "b", "c", "ret"],
    "ABOUT.ELF": ["a", "b", "c", "spc", "ret", "backspace"],
    "UTEXTEDIT.ELF": ["h", "e", "l", "l", "o", "spc", "g", "e", "m", "ret",
                      "x", "y", "z", "backspace", "ret"],
}
CLOSE_BUTTON = {
    "UTERM.ELF": (120 + 80 * 8 + 16 - 14, 113),      # 80x25 cells
    "ABOUT.ELF": (120 + 60 * 8 + 16 - 14, 113),      # 60x18 cells
    "UTEXTEDIT.ELF": (120 + 84 * 8 + 16 - 14, 113),  # 84x28 cells
}
# Mouse path over the windows, the desktop and the dock (no clicks).
STRESS_WIGGLE = [(300, 200), (700, 300), (500, 480), (880, 520), (200, 400),
                 (640, 140), (60, 300)]

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


def mkgemfs(*args):
    return subprocess.run([sys.executable, MKGEMFS] + list(args),
                          capture_output=True)


def random_bytes(size, seed):
    """The same bytes on every run, so a failure can be repeated."""
    return random.Random(seed).randbytes(size)


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


def title_bar_problem(image):
    r, g, b = image.logical_rgb(TITLE_BAR_PROBE)
    if b > 150 and b > r + 60:
        return None
    return "probe #%02x%02x%02x after %.0f s" % (r, g, b, WINDOW_TIMEOUT)


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

    def __init__(self, path, timeout=30.0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                break
            except OSError:
                self.sock.close()
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

    def click(self):
        self.press()
        self.release()

    def press(self):
        self.monitor.cmd("mouse_button 1")
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
        if self.args.vga_mem:
            # (-global VGA.vgamem_mb leaves the machine without a VGA)
            cmd += ["-vga", "none",
                    "-device", "VGA,vgamem_mb=%d" % self.args.vga_mem]
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
        self.monitor = Monitor(sock)
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
        self.monitor.cmd("screendump %s" % ppm)
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
            if bar != (0x20, 0x20, 0x20):
                return "topbar pixel is #%02x%02x%02x" % bar
            if image.logical_rgb(dock) != (0x20, 0x20, 0x20):
                return "dock pixel is #%02x%02x%02x" % image.logical_rgb(dock)
            if wall in ((0, 0, 0), bar):
                return "wallpaper pixel is #%02x%02x%02x" % wall
            return None

        issue, seconds = self.watch_screen("desktop", problem,
                                           DESKTOP_TIMEOUT)
        return self.check(issue is None, "desktop: screendump shows topbar, "
                          "wallpaper and dock",
                          issue or "desktop.png, %.1f s" % seconds)

    def run_app(self, name, menu, item, updating_area):
        spawn_re = re.compile(r"\[PROC\] Spawned PID=(\d+) " + re.escape(name))

        def spawned(text):
            return spawn_re.findall(text)

        before = len(spawned(self.log_text()))
        self.pointer.home()
        self.pointer.move_to(menu)
        self.pointer.click()
        time.sleep(0.4)
        self.pointer.move_to(item)
        # The menu starts the program on the button press. The button stays
        # down until the window check is done, so no input event can make
        # the kernel redraw while the test waits for the window.
        self.pointer.press()
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

            self.check_window_drawn(name)
        finally:
            try:
                self.pointer.release()
            except (OSError, SmokeError):
                pass  # QEMU is gone; the original error is reported

        if updating_area:
            self.check_redraws_by_itself(name, updating_area)

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

    def check_redraws_by_itself(self, name, area):
        # Baseline: a frame that shows the window. The button release causes
        # one more frame, which a kernel that drops syscall redraw requests
        # would draw too, so the baseline must not be older than that frame.
        time.sleep(0.3)
        issue, _ = self.watch_screen("updating", title_bar_problem,
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

    def check_window_drawn(self, name):
        shot = name.split(".")[0].lower()

        issue, seconds = self.watch_screen(shot, title_bar_problem,
                                           WINDOW_TIMEOUT)
        return self.check(issue is None,
                          "%s: window title bar on screen" % name,
                          "%s.png, %s" % (shot, issue or
                                          "%.1f s, no input" % seconds))

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
            for point in STRESS_WIGGLE:
                self.pointer.move_to(point)
            # close from the top window down, alternating Esc and the button
            for (name, _, _, _), pid in reversed(list(zip(APPS, pids))):
                if cycle % 2 == 0:
                    self.monitor.cmd("sendkey esc")
                else:
                    self.pointer.home()
                    self.pointer.move_to(CLOSE_BUTTON[name])
                    self.pointer.click()
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
            if title_bar_problem(image) is None:
                return "a window is still open"
            return None

        time.sleep(1.0)
        issue, _ = self.watch_screen("stress-end", no_window, 5.0)
        self.check(issue is None, "stress: desktop empty at the end",
                   issue or "stress-end.png")
        self.check(True, "stress: %d cycles" % cycles,
                   "%.0f s" % (time.monotonic() - start))

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
        for pattern in self.args.expect_log:
            self.check(re.search(pattern, text) is not None,
                       "serial log: /%s/" % pattern)
        self.check(self.qemu.poll() is None, "QEMU still running at the end")

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
        if self.args.preboot:
            self.preboot()
        self.snapshot_disks()
        boots = 2 if self.args.selftest else 1
        for boot in range(1, boots + 1):
            if boot == 2:
                self.check_between_boots()
            if not self.run_once(boot):
                return
        self.check_disks()

    def run_once(self, boot):
        """One QEMU run of the test; False if it could not finish."""
        self.start_qemu("serial.log" if boot == 1 else "serial-boot2.log")
        try:
            if not self.boot():
                self.save_diagnostics()
                return False
            self.desktop()
            if self.args.selftest:
                self.selftest(boot)
            elif self.args.stress:
                self.stress()
            else:
                for name, menu, item, updating_area in APPS:
                    self.run_app(name, menu, item, updating_area)
                    time.sleep(0.5)
            if not self.args.selftest:
                self.scan_log()
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
    parser.add_argument("--boot", choices=("floppy", "hdd"),
                        default="floppy",
                        help="boot device (--image is the floppy or the "
                        "hard disk image)")
    parser.add_argument("--memory", type=int, default=128, metavar="MB")
    parser.add_argument("--no-data-disk", action="store_true")
    parser.add_argument("--data-disk", choices=("gemfs", "unsigned", "damaged"),
                        default="gemfs",
                        help="a fresh GemFS disk, random bytes, or a GemFS "
                        "disk with a wrong superblock checksum")
    parser.add_argument("--slow-writes", type=int, default=0, metavar="N",
                        help="let QEMU write at most N requests per second "
                        "to the data disk")
    parser.add_argument("--preboot", action="store_true",
                        help="boot once on the new disks before the test")
    parser.add_argument("--expect-unchanged", action="append", default=[],
                        choices=("data", "boot"),
                        help="that disk image must be the same afterwards")
    parser.add_argument("--vga-mem", type=int, default=0, metavar="MB",
                        help="VRAM of the QEMU VGA (default: QEMU's 16)")
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
    for name, extra, logs in MATRIX:
        boot = extra[extra.index("--boot") + 1] if "--boot" in extra \
            else "floppy"
        cmd = [sys.executable, here, "--image", images[boot],
               "--out", os.path.join(args.out, name),
               "--timeout", str(args.timeout / len(MATRIX))] + extra
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
