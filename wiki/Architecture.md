# Architecture

## High-level layout

GemOS is still a monolithic kernel with a kernel-hosted desktop stack. The recent change is that practical userspace now exists and runs in `CPL=3`.

### Main layers

1. Boot chain
2. Kernel core
3. Drivers and graphics
4. Desktop / WM / kernel apps
5. Userland path through syscalls and ELF32

## Boot chain

- stage 1 loads stage 2
- stage 2 enables A20, sets up an initial GDT, enters protected mode, initializes graphics, loads the kernel
- kernel takes ownership of later runtime structures

## Kernel core

Core services already in place:

- heap allocator
- legacy 32-bit paging
- interrupt and exception handling
- PIT tick and RTC time path
- scheduler
- event queue
- serial debug

## Memory model

GemOS uses 32-bit legacy paging with 4 KB pages.

Current transition model:

- one kernel mapping shared across processes
- separate `CR3` per process
- kernel pages stay supervisor-only
- user image and stack live in user-mapped ranges

This keeps the existing kernel desktop alive while still enforcing real isolation for user code.

## Desktop model

These still live in kernel space:

- WM
- desktop shell
- topbar
- dock
- menus
- current GUI rendering path

That is intentional. GemOS is not forcing a giant userspace GUI refactor before thin userland services prove out.

## Userland model

Userland MVP currently includes:

- `CPL=3`
- kernel/user GDT layout
- TSS with `esp0` updates
- per-process address spaces
- `int 0x80`
- safe copy helpers for user pointers
- ELF32 `ET_EXEC` loader

The first practical userspace app path is a terminal:

- terminal logic in userspace
- console session and window hosting in kernel space

This gives real process isolation without needing a full userspace windowing toolkit yet.
