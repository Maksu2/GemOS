#!/usr/bin/env python3
"""Start an extracted GemOS launcher twice and check its GemFS disk."""

import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import zipfile

from package_qemu import PACKAGES
import smoke


PLATFORM = {"darwin": "Apple-Silicon", "win32": "Windows-x64", "linux": "Linux"}
FOLDER = "Release Test"


def unpack_launcher(packages, directory):
    platform = PLATFORM[sys.platform]
    archives = list(packages.glob(f"GemOS-{platform}-*.zip"))
    if len(archives) != 1:
        raise smoke.SmokeError(f"Expected one archive for {platform} in {packages}")
    filename = PACKAGES[platform]
    with zipfile.ZipFile(archives[0]) as archive:
        if archive.namelist() != [filename]:
            raise smoke.SmokeError(f"Expected a single launcher in {archives[0].name}")
        launcher = Path(archive.extract(filename, directory))
        if os.name != "nt":
            launcher.chmod(archive.getinfo(filename).external_attr >> 16 & 0o777)
    return launcher


def monitor_wrapper(directory):
    """Add a loopback monitor and serial log to the real QEMU invocation."""
    if os.name == "nt":
        wrapper = directory / "qemu-monitor.cmd"
        wrapper.write_bytes(
            b'@echo off\r\n'
            b'"%GEMOS_TEST_QEMU%" %* -display none '
            b'-monitor "tcp:127.0.0.1:%GEMOS_TEST_PORT%,server=on,wait=off" '
            b'-serial "file:%GEMOS_TEST_SERIAL%"\r\n'
            b'exit /b %ERRORLEVEL%\r\n')
    else:
        wrapper = directory / "qemu-monitor.sh"
        wrapper.write_text(
            '#!/bin/bash\n'
            'exec "$GEMOS_TEST_QEMU" "$@" -display none '
            '-monitor "tcp:127.0.0.1:$GEMOS_TEST_PORT,server=on,wait=off" '
            '-serial "file:$GEMOS_TEST_SERIAL"\n', encoding="ascii")
        wrapper.chmod(0o755)
    return wrapper


def boot_package(launcher, environment, output, disk, create_folder):
    output.mkdir(parents=True, exist_ok=True)
    args = argparse.Namespace(out=str(output), timeout=240, boot_timeout=90,
                              app_timeout=60, no_acpi=False, no_data_disk=False,
                              data_disk="gemfs")
    run = smoke.Smoke(args)
    run.disks["data"] = str(disk)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        address = reservation.getsockname()

    command = ["cmd.exe", "/d", "/c", "call", launcher.name] if os.name == "nt" else [str(launcher)]
    with (output / "launcher.log").open("wb") as log:
        run.qemu = subprocess.Popen(
            command, cwd=launcher.parent, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT,
            env=dict(environment, GEMOS_TEST_PORT=str(address[1]),
                     GEMOS_TEST_SERIAL=run.serial_log))
        try:
            run.monitor = smoke.Monitor(address, timeout=90)
            run.pointer = smoke.Pointer(run.monitor)
            if not run.boot() or "[GemFS] Mounted GemFS" not in run.log_text():
                raise smoke.SmokeError("GemOS did not reach the desktop with the packaged disk")
            if create_folder:
                explorer = run.open_explorer()
                run.explorer_new_folder(explorer)
                run.explorer_name_screen(explorer, "new-folder", "name field opens")
                run.type_text(FOLDER + "\n")
                run.explorer_name_screen(explorer, "saved", "named folder saved", editing=False)
            run.shutdown()
            run.check_gemfs("after shutdown")
            listing = smoke.mkgemfs("ls", str(disk), "/")
            run.check(listing.returncode == 0 and (FOLDER + "/").encode() in listing.stdout,
                      "the created folder is present on the data disk")
            if not all(result[0] for result in run.results):
                raise smoke.SmokeError("packaged launcher test failed")
        except (OSError, smoke.SmokeError):
            if run.monitor:
                run.save_diagnostics()
            raise
        finally:
            run.stop_qemu()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packages", type=Path, default=Path("build/release"))
    parser.add_argument("--out", type=Path, default=Path("build/package-smoke"))
    args = parser.parse_args()

    if sys.platform not in PLATFORM:
        parser.error("the launcher test supports macOS, Windows, and Linux")
    qemu = next((path for name in (os.environ.get("QEMU"), "qemu-system-i386", "qemu-system-x86_64")
                 if name and (path := shutil.which(name))), None)
    if qemu is None:
        parser.error("install QEMU or set QEMU to the x86 emulator's path")

    with tempfile.TemporaryDirectory(prefix="gemos-package-test-") as temporary:
        directory = Path(temporary)
        data_directory = directory / "saved data, GemOS"
        disk = data_directory / "data.img"
        launcher = unpack_launcher(args.packages, directory)
        env = dict(os.environ, QEMU=str(monitor_wrapper(directory)),
                   GEMOS_TEST_QEMU=qemu, GEMOS_QEMU_DIR=str(data_directory))
        boot_package(launcher, env, args.out / "first", disk, create_folder=True)
        saved_hash = smoke.sha256(disk)
        boot_package(launcher, env, args.out / "second", disk, create_folder=False)
        if saved_hash != smoke.sha256(disk):
            raise smoke.SmokeError("the second start changed the saved disk")
    print(f"PACKAGE: PASS ({PLATFORM[sys.platform]})")


if __name__ == "__main__":
    main()
