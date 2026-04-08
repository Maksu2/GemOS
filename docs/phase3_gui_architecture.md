# Hosted App Transition Architecture

This note replaces an older GUI-planning document that described GemOS before the current userland transition existed.

## Current model

GemOS now uses a hosted-app transition architecture:

- the kernel still owns the desktop shell, WM, topbar, dock and decorations
- userland apps run in Ring 3 with separate address spaces
- apps render into a hosted text surface and receive input events through a thin ABI

## Why this model exists

The goal is to grow practical userspace without forcing a full userspace GUI toolkit too early.

That gives GemOS:

- real process isolation
- real app lifecycle in userspace
- a small syscall surface
- a stable desktop while new app patterns are proven out

## Current app set

- `UTERM.ELF` for interactive terminal workflow
- `ABOUT.ELF` for a small, polished informational app
- `UTEXTEDIT.ELF` for document editing bring-up

## Direction

The next steps are about:

- finishing file-oriented userland apps
- hardening lifecycle and cleanup
- only broadening shared userspace helpers when multiple apps actually need them

This is a transition architecture by design, not an accidental halfway state.
