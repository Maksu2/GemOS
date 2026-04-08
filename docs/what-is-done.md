# GemOS Status Snapshot

GemOS is already beyond the “kernel boots” stage.

## Current state

### Boot and kernel

- own stage 1 + stage 2 boot chain
- protected mode bring-up
- kernel entry at `0x100000`
- heap allocator
- 32-bit legacy paging
- interrupt and exception handling
- PIT, RTC and serial debug

### Desktop stack

- VBE LFB graphics
- page-flipped rendering
- TrueType font rendering
- window manager
- topbar, dock and menus
- PS/2 keyboard and mouse input

### Storage and execution

- ATA PIO
- GemFS
- preemptive round-robin scheduler
- per-process `CR3`
- Ring 3 isolation
- ELF32 `ET_EXEC` loader

### Userland transition

- hosted app model: kernel hosts the window and surface
- userland apps own state, logic and render composition
- `UTERM.ELF`
- `ABOUT.ELF`
- `UTEXTEDIT.ELF` in active bring-up

## What this means

GemOS is in a deliberate transition phase:

- the desktop shell remains in kernel space
- userland is real and isolated
- APIs stay thin until multiple practical apps prove they need more

For the most current overview, use the main [README](../README.md), the [project site](./index.html) and the GitHub wiki.
