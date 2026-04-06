# Milestones and Roadmap

## Milestones reached

### Foundation

- custom boot path
- protected mode kernel
- interrupts, timer, serial
- heap
- paging MVP
- storage and filesystem

### Desktop

- VBE desktop rendering
- WM
- topbar
- dock
- menus
- kernel apps

### Userland transition

- scheduler matured enough for process-backed tasks
- Ring 3 works
- process isolation works
- ELF32 userland loader works
- first real userland terminal path works

## Current recommended focus

1. harden userland MVP with negative and regression tests
2. keep cleanup pressure on bring-up/debug code
3. expand userspace through thin, boring interfaces

## What should probably come next

- stable negative test suite for user faults and bad ELF inputs
- cleaner console service and userspace terminal polish
- a second small userland app only after the terminal path proves itself

## What should not happen yet

- general-purpose userspace GUI toolkit
- POSIX surface area
- fork/exec
- dynamic linker
- shared memory
- desktop-wide rewrite to userspace
