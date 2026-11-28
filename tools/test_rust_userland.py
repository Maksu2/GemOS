#!/usr/bin/env python3
"""Run the same Rust demo ELF on several QEMU x86 CPU models."""

import argparse
from pathlib import Path
import re
import shutil
import signal
import subprocess

import smoke


# Exercise both x87 save paths and two later CPU families. The legacy
# profile also uses the smallest desktop configuration tested by CI.
CPU_PROFILES = (
    ("pentium2", "pentium2,-fxsr", 32, 4, (1280, 800), "FNSAVE"),
    ("qemu32", "qemu32", 128, 16, (1920, 1080), "FXSAVE"),
    ("nehalem", "Nehalem", 128, 16, (1920, 1080), "FXSAVE"),
    ("opteron", "Opteron_G1", 128, 16, (1920, 1080), "FXSAVE"),
)
DEMO_SPAWN = re.compile(r"\[PROC\] Spawned PID=(\d+) /RUSTDEMO\.ELF\r?\n")
DEMO_READY = "[RUST] GemFS example copied"
DEMO_PANIC = "[RUST] application panicked"


def open_demo(run, point, shot):
    since = len(run.log_text())
    run.pointer.move_to(point)
    run.pointer.click()
    # The first click selects the row. Clicking a selected row opens it.
    if not DEMO_SPAWN.search(run.log_text(), since):
        run.monitor.cmd("sendkey ret")
    spawned = run.wait_for(lambda log: DEMO_SPAWN.search(log, since),
                           run.args.app_timeout)
    if not spawned:
        raise smoke.SmokeError("File Explorer did not start RUSTDEMO.ELF")
    pid = spawned[1]
    ready = run.wait_for(lambda log: DEMO_READY in log[since:],
                         run.args.app_timeout)
    if not run.check(bool(ready), "Rust opened a console and copied the GemFS file",
                     f"PID={pid}"):
        raise smoke.SmokeError("RUSTDEMO.ELF did not reach its event loop")
    window = run.console_window(pid)
    run.check_window_drawn("RUSTDEMO.ELF", window, shot)
    return pid, window


def check_exit(run, pid, code, action):
    reaped = re.compile(rf"\[PROC\] Reaped PID={pid} exit=(-?\d+)\r?\n")
    match = run.wait_for(reaped.search, run.args.app_timeout)
    if not run.check(bool(match) and int(match[1]) == code, action,
                     f"PID={pid} exit={match[1] if match else 'missing'}"):
        raise smoke.SmokeError(f"RUSTDEMO.ELF did not exit with status {code}")


def test_profile(args, profile):
    name, cpu, memory, vram, resolution, fpu = profile
    smoke.set_resolution(*resolution)
    run = smoke.Smoke(argparse.Namespace(
        image=args.image, out=str(Path(args.out) / name),
        timeout=100, boot_timeout=30, app_timeout=15, boot="floppy",
        memory=memory, cpu=cpu, vga_mem=vram, no_acpi=False, slow_writes=0,
        data_disk="gemfs", no_data_disk=False, selftest=False, leftovers=False,
        expect_log=[rf"\[FPU\] Per-task state with {fpu}"],
    ))
    print(f"== Rust on {cpu} ({memory} MB RAM)", flush=True)
    run.prepare_disks()
    data = run.disks["data"]
    shutil.copyfile(args.data, data)
    example = smoke.mkgemfs("cat", data, "/Rust example.txt")
    example.check_returncode()

    try:
        run.start_qemu()
        if not run.boot():
            raise smoke.SmokeError("GemOS did not boot")
        explorer = run.open_explorer(root=True)
        # The seeded Rust ELF is the first entry on this GemFS disk.
        point = smoke.explorer_entry(explorer, 0)

        pid, _ = open_demo(run, point, "rust-demo")
        run.monitor.cmd("sendkey esc")
        check_exit(run, pid, 0, "Rust receives Escape and exits normally")

        pid, _ = open_demo(run, point, "rust-before-panic")
        run.monitor.cmd("sendkey p")
        check_exit(run, pid, 101, "Rust panic exits the process with status 101")
        run.check(DEMO_PANIC in run.log_text(), "Rust panic is reported on serial")

        pid, window = open_demo(run, point, "rust-after-panic")
        run.pointer.move_to(window.close_button())
        run.pointer.click()
        check_exit(run, pid, 0, "The desktop can relaunch and close the app after a panic")

        run.scan_log()
        run.shutdown()
        run.check_gemfs("after shutdown")
        saved = smoke.mkgemfs("cat", data, "/Rust copy.txt")
        run.check(saved.returncode == 0 and saved.stdout == example.stdout,
                  "The file written by Rust matches the original byte for byte")
    except (OSError, smoke.SmokeError):
        if run.monitor:
            run.save_diagnostics()
        raise
    finally:
        run.stop_qemu()

    if not all(ok for ok, _, _ in run.results):
        raise smoke.SmokeError(f"failed checks on {cpu}")
    print(f"RUST: PASS ({cpu}, {len(run.results)} checks)", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="build/gemos.img")
    parser.add_argument("--data", default="build/rust/data.img")
    parser.add_argument("--out", default="build/rust/smoke")
    args = parser.parse_args()

    def on_signal(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    failed = []
    for profile in CPU_PROFILES:
        try:
            test_profile(args, profile)
        except (OSError, subprocess.CalledProcessError, smoke.SmokeError) as error:
            failed.append(profile[0])
            print(f"RUST: FAIL ({profile[0]}, {error})", flush=True)
    if failed:
        print(f"RUST: FAIL ({', '.join(failed)})", flush=True)
        return 1
    print(f"RUST: PASS ({len(CPU_PROFILES)} CPU profiles)", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
