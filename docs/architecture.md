# GemOS Architecture

GemOS is built on a monolithic kernel architecture with a tightly integrated graphical subsystem. The architecture prioritizes low latency and direct manipulation of display resources.

## System Stack

The system is divided into clearly defined layers:

```
+---------------------------------------------------+
|                  User Applications                |
|               (Written in GemLang)                |
+---------------------------------------------------+
|                      GemRun                       |
|           (Interpreter & Runtime Layer)           |
+---------------------------------------------------+
|                     GemShell                      |
|         (Window Manager, Taskbar, Menus)          |
+---------------------------------------------------+
|                     GemCore                       |
|  (Kernel, Memory Manager, VESA Driver, IO, ISR)   |
+---------------------------------------------------+
|                    Hardware                       |
|             (x86 Architecture / QEMU)             |
+---------------------------------------------------+
```

## 1. GemCore (The Kernel)
*   **Type:** 32-bit Monolithic Kernel (x86).
*   **Boot Process:** Custom bootloader implementing LBA disk reading to load the kernel above the 1MB mark.
*   **Graphics:** VESA BIOS Extensions (VBE) implementation providing a linear framebuffer. Supports dynamic resolution detecting.
*   **Input:** Interrupt-driven keyboard and mouse drivers (PS/2).

## 2. GemShell (The Window Manager)
*   **Compositor:** A software compositor that manages a Z-ordered list of windows.
*   **Painting:** Uses a "Painter's Algorithm" with dirty rect optimization (planned) for rendering.
*   **Widgets:** Provides native GUI primitives (Windows, Buttons, Labels) directly to the kernel space.

## 3. GemRun (The Runtime)
*   Computes the logic for user applications.
*   Contains the **GemLang Interpreter**, which parses and executes application scripts natively.
*   Manages the lifecycle of applications (Launch, Suspend, Terminate).

## 4. Application Integration
Applications are not binary executables in the traditional sense (ELF/PE). They are GemLang script bundles.
*   **Discovery:** The system scans a virtual `/apps` directory at startup.
*   **Execution:** Selecting an app triggers the interpreter to parse the script and build the in-memory window structures.
*   **Sandboxing:** Applications operate within the constraints of the interpreter, preventing memory corruption of the kernel.
