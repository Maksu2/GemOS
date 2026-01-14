# GemOS Overview

**GemOS** is an experimental, long-term operating system project designed to explore computer system architecture from a holistic perspective. It is not an attempt to clone existing systems like Linux, Windows, or macOS, nor is it a hobbyist toy OS. Instead, it serves as a research platform for investigating how a system looks when the kernel, graphical user interface, and application runtime are co-designed as a single, cohesive unit.

## Project Identity

### What GemOS Is
*   **A Vertical Slice:** GemOS implements everything from the bootloader and kernel (GemCore) to the window manager (GemShell) and the application language (Gem).
*   **A Research Platform:** It prioritizes architectural experimentation, strict adherence to design philosophies, and simplicity over raw performance or broad hardware compatibility.
*   **An Engineering Artifact:** It demonstrates a deliberate engineering process, evolved iteratively over a sustained period.

### What GemOS Is Not
*   **A Daily Driver:** It currently runs in an emulator (QEMU) and lacks drivers for real-world hardware networking or audio.
*   **A UNIX Clone:** While it may borrow concepts, it does not strictly adhere to POSIX standards where they conflict with the system's design goals.

## Core Problem Space

GemOS explores several key questions in operating system design:
1.  **Complexity vs. Cohesion:** Can a system be simplified by tightly coupling the application layer with the kernel's display primitives?
2.  **State-Driven Interfaces:** How can reactive UI paradigms (common in web development) be implemented efficiently at the native system level without massive frameworks?
3.  **Language-OS Symbiosis:** What advantages arise when the application language (Gem) is designed specifically for the OS's internal APIs?

## Target Audience

This project is intended for:
*   **Systems Students & Educators:** As a readable reference for OS implementation details (paging, interrupts, VESA graphics).
*   **UI/UX Researchers:** Interested in alternative desktop environments and window management philosophies.
*   **Language Designers:** Exploring domain-specific languages for system applications.
