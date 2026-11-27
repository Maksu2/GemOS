#!/usr/bin/env python3
"""Package a tested GemOS floppy image for QEMU and BIOS CD boot."""

import argparse
import base64
import gzip
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
PACKAGES = {
    "Apple-Silicon": "RunGemOS.command",
    "Windows-x64": "RunGemOS.bat",
    "Linux": "RunGemOS.sh",
}


def payload(data):
    return base64.encodebytes(gzip.compress(data, mtime=0)).decode("ascii").rstrip()


def write_package(directory, platform, version, values):
    launcher = PACKAGES[platform]
    windows = platform == "Windows-x64"
    template = TOOLS / "release" / ("launch-gemos.bat.in" if windows else "launch-gemos.sh.in")
    content = template.read_text(encoding="utf-8")
    for key, value in values.items():
        content = content.replace("@" + key + "@", value)
    if windows:
        content = content.replace("\n", "\r\n")

    archive = directory / f"GemOS-{platform}-{version}.zip"
    entry = zipfile.ZipInfo(launcher)
    entry.create_system = 3
    entry.external_attr = 0o100755 << 16
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zip_file:
        zip_file.writestr(entry, content, compress_type=zipfile.ZIP_DEFLATED)
    return archive


def package(image, output, version):
    image_bytes = image.read_bytes()
    if len(image_bytes) != 1440 * 1024 or image_bytes[510:512] != b"\x55\xaa":
        raise ValueError("expected a bootable 1.44 MiB GemOS floppy image")

    mit = (ROOT / "LICENSE").read_text(encoding="utf-8")
    ofl = (ROOT / "assets" / "OFL.txt").read_text(encoding="utf-8")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output) as temporary:
        directory = Path(temporary)
        disk = directory / "data.img"
        subprocess.run([sys.executable, str(TOOLS / "mkgemfs"), "format", str(disk),
                        "--size", "10M"], check=True)
        disk_bytes = disk.read_bytes()
        values = {
            "VERSION": version,
            "LICENSES": "\n".join("# " + line for line in (mit + "\n" + ofl).splitlines()),
            "BOOT64": payload(image_bytes),
            "DATA64": payload(disk_bytes),
            "BOOT_SHA": hashlib.sha256(image_bytes).hexdigest(),
            "DATA_SHA": hashlib.sha256(disk_bytes).hexdigest(),
        }
        assets = [write_package(directory, platform, version, values) for platform in PACKAGES]

        cdroot = directory / "cdroot"
        cdroot.mkdir()
        shutil.copyfile(image, cdroot / "gemos.img")
        (cdroot / "LICENSE.TXT").write_text(mit, encoding="utf-8")
        (cdroot / "OFL.TXT").write_text(ofl, encoding="utf-8")
        iso = directory / f"GemOS-{version}.iso"
        subprocess.run(["genisoimage", "-quiet", "-V", "GEMOS", "-b", "gemos.img",
                        "-c", "boot.cat", "-o", str(iso), str(cdroot)], check=True)
        assets.append(iso)

        checksums = directory / "SHA256SUMS.txt"
        checksums.write_text("".join(
            f"{hashlib.sha256(asset.read_bytes()).hexdigest()}  {asset.name}\n"
            for asset in sorted(assets)), encoding="ascii")
        for asset in assets + [checksums]:
            destination = output / asset.name
            asset.replace(destination)
            print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=ROOT / "build" / "gemos.img")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "release")
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
        parser.error("version must be safe to use in a file name")
    package(args.image, args.out, args.version)


if __name__ == "__main__":
    main()
