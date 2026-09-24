# GemOS

From-scratch 32-bit desktop operating system for x86, written in C and assembly.

[Project site](http://gemos.maksu.online) • [Wiki](https://github.com/Maksu2/GemOS/wiki) • [Repository](https://github.com/Maksu2/GemOS)

<p align="center">
  <img src="docs/assets/screenshots/desktop-overview.png" alt="GemOS desktop overview" width="900">
</p>

<table>
  <tr>
    <td><img src="docs/assets/screenshots/uterm.png" alt="UTERM userland terminal" width="280"></td>
    <td><img src="docs/assets/screenshots/about.png" alt="About GemOS userland app" width="280"></td>
    <td><img src="docs/assets/screenshots/utextedit.png" alt="UTEXTEDIT userland text editor" width="280"></td>
  </tr>
  <tr>
    <td><strong>UTERM.ELF</strong><br>Hosted userland terminal</td>
    <td><strong>ABOUT.ELF</strong><br>Small informational userland app</td>
    <td><strong>UTEXTEDIT.ELF</strong><br>Userland text editor in active bring-up</td>
  </tr>
</table>

GemOS is a classic desktop-style operating system built end-to-end: bootloader, protected-mode bring-up, kernel, memory management, interrupt path, graphics stack, filesystem, scheduler, Ring 3 transition and now practical userland applications.

The project goal is not novelty for its own sake. The goal is to build a calm, coherent, technically honest system where architecture comes first and big rewrites are earned, not assumed.

## Why this project matters

- It owns the whole path from the boot sector to the desktop.
- It treats “boring is success” as an engineering rule, not a slogan.
- It is already past the fake userland stage: isolated Ring 3 processes run real apps while the desktop remains stable.

## What works now

### Boot and kernel foundations

- 2-stage bootloader
- A20, protected mode, kernel entry
- kernel-owned GDT for ring 0 / ring 3
- TSS, `ltr`, and `esp0` stack switching
- IDT, ISR / IRQ handling, PIC remap, PIT, RTC
- serial debug path

### Memory and execution

- heap allocator
- 32-bit legacy paging with 4 KB pages
- separate `CR3` per process
- shared supervisor-only kernel mapping as the transition model
- round-robin scheduler with an idle task and blocking waits; only ring 3 code is preempted, kernel code runs until it gives up the CPU
- `process_t` / `task_t` split
- user processes that fault with #DE, #UD, #TS, #NP, #SS, #GP or #PF are killed and reaped; any other exception raised in ring 3 still halts the whole system

### Desktop, drivers and storage

- VBE LFB graphics
- page-flipped rendering path
- PS/2 keyboard and mouse
- TrueType font rendering
- window manager
- topbar, dock, menus and focus model
- ATA PIO
- GemFS

### Userland transition

- Ring 3 (`CPL=3`)
- ELF32 static `ET_EXEC` loader
- `int 0x80` syscall layer
- safe copy helpers for user pointers
- hosted app model: kernel hosts the window/surface, userland owns app state and render logic
- hosted close requests and keyboard modifiers
- thin file read/write syscalls (no app uses them yet)

## Current architecture

GemOS is intentionally in a transition architecture:

- the desktop shell, WM, topbar, dock and window decorations remain in kernel space
- userland applications run as isolated processes with their own address spaces
- userland apps talk to the system through a small syscall surface and a hosted text-surface window model

That is deliberate. GemOS is not trying to jump straight from “all apps in kernel space” to “full userspace GUI toolkit” in one rewrite.

```text
boot/ stage1 + stage2
        ↓
kernel core: interrupts, paging, heap, scheduler, ELF, syscalls
        ↓
drivers and graphics: VBE, PS/2, ATA, serial, fonts
        ↓
desktop shell: WM, topbar, dock, menus, kernel-hosted windows
        ↓
hosted app services: console/window surface, input delivery, file I/O
        ↓
userland apps: UTERM.ELF, ABOUT.ELF, UTEXTEDIT.ELF
```

## Current userland

| App | State | What it proves |
| --- | --- | --- |
| `UTERM.ELF` | Usable | Real Ring 3 terminal with input, output and hosted-window lifecycle |
| `ABOUT.ELF` | Stable | Small polished userland app with timed updates and clean close flow |
| `UTEXTEDIT.ELF` | Active bring-up | Hosted editor with document state, multiline render, caret movement and dirty state |

The current text editor is intentionally in progress. Basic document editing is in place; file open/save and the unsaved-close flow come after the kernel concurrency work (see [Roadmap](#roadmap)).

## Build / Run / Debug

### Tooling

- `nasm`
- `qemu-system-i386`
- an `i686-elf-*` or `x86_64-elf-*` cross toolchain, or a host `gcc`/`binutils` that accepts `-m32` (the Makefile falls back to it automatically; CI uses this path)
- Python 3 for the smoke test

On macOS:

```bash
brew install nasm qemu i686-elf-gcc
```

On Debian/Ubuntu (host `gcc -m32` fallback):

```bash
sudo apt-get install nasm qemu-system-x86 gcc
```

Build, test and run:

```bash
make all          # build/gemos.img
tools/smoke.sh    # build, boot headless in QEMU, start UTERM/ABOUT/UTEXTEDIT
tools/smoke.sh --stress   # 25 cycles of opening, typing into and closing them
make run          # QEMU window with the GemFS data disk (build/data.img)
```

`tools/smoke.sh` prints one PASS/FAIL line per check and keeps the serial log and screenshots in `build/smoke/` (`build/stress/` for the stress test). CI runs `make all`, the smoke test and the stress test on every push and pull request.

Run under GDB:

```bash
make debug
```

Then in another shell:

```bash
i686-elf-gdb build/kernel.elf
target remote :1234
```

## Repo map

```text
boot/       stage1 + stage2 boot chain
kernel/     kernel core, scheduler, paging, ELF, syscalls, WM
drivers/    hardware drivers
apps/       kernel-space apps and desktop launchers
userland/   user-space binaries, runtime and shared helpers
include/    freestanding C headers and the userland ABI (include/gemos)
lib/        freestanding support code
assets/     system font (Inter) and its license
tools/      QEMU smoke test
docs/       GitHub Pages site and the code audit
```

## Roadmap

The work follows the stages of the [September 2026 code audit](docs/AUDIT-2026-09.md) (section 8.2). No new features before stage 3 is done:

0. Safety net: CI and the QEMU smoke test (done)
1. Clean-up: dead code, duplicated headers, stale docs (done)
2. Boot and memory: zeroed BSS, boot info with the E820 map, memory detection, a new loader, an ATA driver with timeouts
3. Concurrency and isolation: kernel code is not preempted and waits block (done); FPU state, a fault in ring 3 kills only the process, a hardened ELF loader and heap
4. Storage: GemFS with a superblock and allocation, files larger than 8 KB
5. GUI and apps: clipping, window placement, `UTEXTEDIT.ELF` open/save, then retiring the kernel text editor

## Out of scope right now

Not the current focus:

- POSIX compatibility
- `fork/exec`
- dynamic linking
- a large userspace GUI toolkit
- USB
- TCP/IP
- audio
- a high-half kernel rewrite
- moving the entire desktop stack to userspace in one jump

## Links

- Project site: http://gemos.maksu.online
- Wiki: https://github.com/Maksu2/GemOS/wiki
- Repository: https://github.com/Maksu2/GemOS

## License

GemOS is released under the MIT License, see [LICENSE](LICENSE).

The bundled system font `assets/font.ttf` is [Inter](https://github.com/rsms/inter) 4.001, © The Inter Project Authors, licensed under the SIL Open Font License 1.1, see [assets/OFL.txt](assets/OFL.txt).
