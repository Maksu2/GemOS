# GemOS

> A from-scratch 32-bit desktop operating system for x86, written in C and asm.

[![Status](https://img.shields.io/badge/status-active-2f7d32)](https://github.com/Maksu2/GemOS)
[![Architecture](https://img.shields.io/badge/arch-x86%2032--bit-005bbb)](https://github.com/Maksu2/GemOS)
[![Boot](https://img.shields.io/badge/boot-own%20stage1%20%2B%20stage2-1f2937)](https://github.com/Maksu2/GemOS)
[![Userland](https://img.shields.io/badge/userland-ring%203%20MVP-b45309)](https://github.com/Maksu2/GemOS)
[![Pages](https://img.shields.io/badge/site-GitHub%20Pages-7c3aed)](https://maksu2.github.io/GemOS/)
[![Wiki](https://img.shields.io/badge/docs-Wiki-0f766e)](https://github.com/Maksu2/GemOS/wiki)

GemOS is not a toy kernel and not a “look, it booted” demo. It is a classic desktop-style OS built end-to-end with its own boot chain, memory management, interrupt path, GUI stack, filesystem, scheduler, and now a working Ring 3 / userland MVP.

The project goal is simple:

- build a calm, usable, technically coherent desktop OS
- keep the architecture boring, stable, and understandable
- avoid dependency creep, framework creep, and fake complexity

## Why GemOS

GemOS is built around a few hard rules:

- **System, not demo**: features are added only when they fit the architecture.
- **Boring is success**: stability beats novelty.
- **Architecture before features**: infrastructure comes first.
- **Own the whole stack**: bootloader, kernel, GUI, filesystem, userland path.

## What Works Now

### Boot and kernel foundations

- 2-stage bootloader
- A20, protected mode, kernel entry
- kernel-owned GDT for ring 0 / ring 3
- TSS, `ltr`, and kernel stack switching via `esp0`
- IDT, ISR/IRQ handling, PIC remap, PIT, RTC
- serial debug output

### Memory and execution

- heap allocator
- legacy 32-bit paging with 4 KB pages
- per-process `CR3`
- shared supervisor-only kernel mapping as transition model
- preemptive round-robin scheduler
- `process_t` / `task_t` split

### Desktop and drivers

- VBE LFB graphics
- page-flipped rendering path
- PS/2 keyboard and mouse
- window manager
- topbar, dock, menus
- TrueType font rendering

### Storage and apps

- ATA PIO
- GemFS
- kernel-space desktop apps
- ELF32 static loader MVP
- `int 0x80` syscall layer
- first userland path with Ring 3 isolation
- first real userland terminal path via kernel-hosted console service

## Current Milestone

GemOS currently sits at this transition point:

- kernel desktop remains alive and stable
- userland is real, not simulated
- user processes can run in `CPL=3`
- faults in userland kill the process instead of panicking the whole kernel
- the next step is expanding practical userspace, not redesigning the kernel again

## Userland MVP

Already implemented:

- `CPL=3`
- separate `CR3` per process
- ELF32 `ET_EXEC` loader
- `int 0x80`
- safe `copy_from_user` / `copy_to_user`
- minimal syscalls:
  - `SYS_exit`
  - `SYS_yield`
  - `SYS_debug_write`
  - `SYS_getpid`
  - `SYS_ticks_ms`
  - `SYS_console_open`
  - `SYS_console_write`
  - `SYS_console_poll_event`
  - `SYS_console_clear`

This is intentionally small. No POSIX layer, no `fork`, no dynamic linker, no large userspace SDK.

## First Real User App

The first practical userland app is `UTERM.ELF`.

Design choice:

- WM, desktop, topbar, dock, and decorations stay in kernel space
- the terminal logic itself runs in userland
- the console window is kernel-hosted, text-oriented, and deliberately thin

That gives GemOS a real userspace app without forcing a full userspace GUI API too early.

## Repo Map

```text
boot/              boot chain: stage1 + stage2
kernel/            kernel core, scheduler, paging, ELF, syscalls, WM
drivers/           hardware drivers
apps/              kernel-space desktop apps and launchers
userland/          user-space binaries and tiny runtime
docs/              GitHub Pages site and project docs
lib/               freestanding support code
```

## Build

### Tooling

- `nasm`
- `make`
- `qemu-system-i386`
- `i686-elf-*` or `x86_64-elf-*` cross-toolchain recommended

### macOS

```bash
brew install nasm qemu
```

### Linux

```bash
sudo apt install nasm qemu-system-x86
```

## Run

```bash
make all
make run
```

For GDB:

```bash
make debug
```

Then in another terminal:

```bash
i686-elf-gdb build/kernel.elf
target remote :1234
```

## Project Links

- Site: https://maksu2.github.io/GemOS/
- Wiki: https://github.com/Maksu2/GemOS/wiki
- Repo: https://github.com/Maksu2/GemOS

## What GemOS Is Not Doing Right Now

Not in the current scope:

- USB
- TCP/IP
- audio
- dynamic linking
- `fork/exec`
- POSIX compatibility
- shared memory
- high-half kernel rewrite
- moving the whole desktop to userspace in one jump

## Design Direction

GemOS is intentionally moving in small, defensible steps:

1. own the boot process
2. own the kernel and desktop stack
3. add isolation without breaking the existing system
4. introduce userspace through thin, stable interfaces
5. only generalize APIs after at least two real consumers need them

## License

MIT
