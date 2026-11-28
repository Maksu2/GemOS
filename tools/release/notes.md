GemOS is a 32-bit x86 desktop OS. This test release includes the graphite desktop, a labelled dock, a text editor and a File Explorer written in Rust that runs as a user program: it creates folders and text files, renames, copies, pastes and deletes entries on GemFS. Clicks, opening and closing windows redraw only what changed, which keeps the desktop responsive under QEMU's software emulation.

### Run in QEMU

| Host | Download | Start after extracting |
| --- | --- | --- |
| Apple Silicon Mac | `GemOS-Apple-Silicon-*.zip` | Double-click `RunGemOS.command` |
| Windows x64 | `GemOS-Windows-x64-*.zip` | Double-click `RunGemOS.bat` |
| Linux | `GemOS-Linux-*.zip` | Run `./RunGemOS.sh` |

The ZIPs contain the launcher and its embedded disk images. The Mac launcher uses Homebrew to install QEMU when needed. Windows uses WinGet when available. On Linux, install your distribution's desktop QEMU package first.

The launchers keep a `data.img` in the host's application data directory so you can keep using your files after switching to a newer launcher. Use **GemOS → Shutdown** to finish a session.

### Bootable ISO

Attach `GemOS-*.iso` as a CD-ROM in an x86 VM with a legacy BIOS and boot from the CD. For writable storage, attach a GemFS image as a separate IDE disk. `SHA256SUMS.txt` lists the hashes of the four downloads.
