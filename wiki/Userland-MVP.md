# Userland MVP

## What is already done

GemOS has crossed the line from "kernel-only desktop" to a real, isolated userland MVP.

Implemented:

- `CPL=3`
- separate `CR3` per process
- kernel-owned GDT for ring 0 / ring 3
- TSS, `ltr`, `esp0` switching
- scheduler aware of `process_t` and `task_t`
- `int 0x80`
- `copy_from_user` / `copy_to_user`
- kill user process on fault without panicking the whole kernel
- ELF32 static `ET_EXEC` loader
- `USRSMOKE.ELF`
- `UTERM.ELF` path for the first practical user app

## Syscall philosophy

The syscall layer is intentionally small.

Current base syscalls:

- `SYS_exit`
- `SYS_yield`
- `SYS_debug_write`
- `SYS_getpid`
- `SYS_ticks_ms`

Console-oriented additions:

- `SYS_console_open`
- `SYS_console_write`
- `SYS_console_poll_event`
- `SYS_console_clear`

## Why the first real app is a terminal

The first userspace app should exercise:

- process lifecycle
- input delivery
- output path
- fault containment
- tiny shared ABI

A terminal does all of that without requiring a full userspace GUI API.

## What stays in kernel space for now

- WM
- window hosting
- decorations
- focus routing
- desktop shell

This is a deliberate transition architecture, not a compromise by accident.
