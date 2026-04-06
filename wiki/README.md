# GemOS Wiki

GemOS is a from-scratch 32-bit desktop operating system for x86, written in C and asm.

It is built around a simple rule: boring is success. The project prefers small, stable, technically defensible steps over flashy rewrites.

## What GemOS already has

- custom stage 1 + stage 2 boot chain
- protected mode kernel entry
- heap, paging MVP, interrupts, PIT, RTC, serial debug
- PS/2 keyboard and mouse
- VBE LFB graphics and page-flipped rendering
- window manager, topbar, dock, menus, font rendering
- ATA PIO and GemFS
- preemptive round-robin scheduler
- Ring 3 / per-process CR3 / ELF32 userland MVP
- first practical userland terminal path via `UTERM.ELF`

## Read this first

- [Architecture](Architecture)
- [Build and Debug](Build-and-Debug)
- [Userland MVP](Userland-MVP)
- [Milestones and Roadmap](Milestones-and-Roadmap)

## Project links

- Repository: https://github.com/Maksu2/GemOS
- Pages: https://maksu2.github.io/GemOS/

## Scope discipline

GemOS is not trying to become everything at once. Current priorities are:

1. keep the existing desktop stable
2. harden userland MVP
3. expand userspace through thin, proven interfaces

Not in the immediate scope:

- USB
- TCP/IP
- audio
- dynamic linking
- fork/exec
- shared memory
- POSIX layer
- moving the entire desktop to userspace in one jump
