GemOS is a 32-bit x86 desktop OS. This test release includes the graphite desktop, a labelled dock and a File Explorer that can create and rename entries on GemFS.

### Run in QEMU

| Host | Download | Start after extracting |
| --- | --- | --- |
| Apple Silicon Mac | `GemOS-Apple-Silicon-*.zip` | Double-click `Uruchom GemOS.command` |
| Windows x64 | `GemOS-Windows-x64-*.zip` | Double-click `Uruchom GemOS.cmd` |
| Linux | `GemOS-Linux-*.zip` | Run `./uruchom-gemos.sh` |

The ZIPs contain the launcher and its embedded disk images. The Mac launcher uses Homebrew to install QEMU when needed. Windows uses WinGet when available. On Linux, install your distribution's desktop QEMU package first.

The launchers keep a `data.img` in the host's application data directory so you can keep using your files after switching to a newer launcher. Use **GemOS → Shutdown** to finish a session.

### Bootable ISO

Attach `GemOS-*.iso` as a CD-ROM in an x86 VM with a legacy BIOS and boot from the CD. For writable storage, attach a GemFS image as a separate IDE disk. `SHA256SUMS.txt` lists the hashes of the four downloads.
