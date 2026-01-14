# GemOS Roadmap

This roadmap outlines the research and engineering goals for the future of GemOS. It is a living document and subject to change based on experimental results.

## Phase 1: Foundation (Complete)
*   [x] Bootloader with LBA support.
*   [x] Monolithic Kernel (GemCore) initialization.
*   [x] VESA Graphics Driver.
*   [x] Basic Window Manager (GemShell).
*   [x] GemLang v1.0 (Interpreter).

## Phase 2: Refinement & Architecture (Current)
*   [ ] **GemLang 2.0:** Full implementation of the layout engine (VStack/HStack) and state binding.
*   [ ] **Filesystem:** Implementation of a read/write virtual filesystem (FAT16/32 or Custom).
*   [ ] **Stability:** Elimination of system halts on application errors.

## Phase 3: Advanced Capabilities (Research)
*   **Networking:** Implementing a minimal TCP/IP stack to explore networked GemLang applications.
*   **Audio:** Sound blaster / AC97 driver implementation.
*   **Vector Graphics:** Moving from bitmap fonts to scalable vector UI elements.

## Phase 4: Self-Hosting
*   The ultimate engineering goal is to be able to develop GemLang applications *within* GemOS itself, using a native GemLang code editor.
