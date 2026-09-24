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
  * the serial log has no PANIC, user fault or allocation failure, and no
    line was printed into the middle of another one.

Artifacts (serial log, QEMU output, PNG screenshots) go to --out.
Only the Python standard library is used.

Exit status: 0 = PASS, 1 = FAIL, 2 = setup error.
"""

import argparse
import os
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

# The desktop runs at 1920x1080 with ui_scale 2.0 (kernel/ui/ui_scale.c), so
# PS/2 mouse deltas and all coordinates below are logical (960x540) pixels.
UI_SCALE = 2
SCREEN_SIZE = (1920, 1080)

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

# How long a frame may take to show up on its own (seconds). The first
# window of a font size is the slowest: its glyphs are rasterized on the
# first render (about 1 s under TCG).
DESKTOP_TIMEOUT = 20.0
WINDOW_TIMEOUT = 10.0

FAILURE_PATTERNS = [
    r"PANIC",
    r"\[USERFAULT\]",
    r"Faulted PID",
    r"allocation failed",
    r"Alloc failed",
    r"Failed to",
    r"\[ELF\]",
]

# Kernel log lines start with a "[Tag] " prefix; indented lines continue the
# previous one. A tag after other text means that one line was printed into
# the middle of another.
LOG_TAG = re.compile(r"\[[A-Za-z][A-Za-z0-9_]*\] ")


class SmokeError(Exception):
    pass


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
        for _ in range(30):
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

    # -- reporting ---------------------------------------------------------
    def check(self, ok, name, detail=""):
        self.results.append((ok, name, detail))
        line = "%s  %s" % ("PASS" if ok else "FAIL", name)
        if detail:
            line += "  (%s)" % detail
        print(line, flush=True)
        return ok

    # -- QEMU ----------------------------------------------------------------
    def start_qemu(self):
        qemu = shutil.which(os.environ.get("QEMU", "qemu-system-i386"))
        if not qemu:
            raise SmokeError("qemu-system-i386 not found (set QEMU=...)")
        if not os.path.isfile(self.args.image):
            raise SmokeError("disk image %s not found, run make all"
                             % self.args.image)

        if os.path.isdir(self.out):
            shutil.rmtree(self.out)
        os.makedirs(self.out)
        floppy = os.path.join(self.out, "gemos.img")
        data = os.path.join(self.out, "data.img")
        shutil.copyfile(self.args.image, floppy)
        with open(data, "wb") as f:
            f.truncate(10 * 1024 * 1024)

        # unix socket paths are limited to ~108 bytes; keep it short
        self.sockdir = tempfile.mkdtemp(prefix="gemos-smoke-")
        sock = os.path.join(self.sockdir, "hmp.sock")
        cmd = [
            qemu,
            "-drive", "file=%s,if=floppy,format=raw" % floppy,
            "-drive", "file=%s,if=ide,format=raw" % data,
            "-m", "128M",
            "-display", "none",
            "-serial", "file:%s" % self.serial_log,
            "-monitor", "unix:%s,server,nowait" % sock,
            "-no-reboot",
        ]
        print("qemu: %s" % " ".join(cmd), flush=True)
        self.qemu_log = open(os.path.join(self.out, "qemu.log"), "wb")
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
        topbar = (300, 5)
        dock = (900, 530)
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
        self.check(self.qemu.poll() is None, "QEMU still running at the end")

    def run(self):
        self.start_qemu()
        try:
            if not self.boot():
                return
            self.desktop()
            for name, menu, item, updating_area in APPS:
                self.run_app(name, menu, item, updating_area)
                time.sleep(0.5)
            self.scan_log()
        finally:
            self.stop_qemu()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--image", default="build/gemos.img")
    parser.add_argument("--out", default="build/smoke")
    parser.add_argument("--timeout", type=float, default=420.0,
                        help="overall time budget in seconds")
    parser.add_argument("--boot-timeout", type=float, default=120.0)
    parser.add_argument("--app-timeout", type=float, default=45.0)
    args = parser.parse_args()

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


if __name__ == "__main__":
    sys.exit(main())
