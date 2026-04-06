# GemOS Phase 3: GUI Framework & UX Architecture

This document outlines the architectural and design decisions for the GemOS Graphical User Interface (GemUI).

## 1. System Architecture

GemOS currently operates in a single address space with a main execution loop. The GUI will be built as an integrated subsystem within the kernel (monolithic UI), functioning similarly to a "Game Engine" or early windowing systems (Classic Mac OS).

### 1.1 Layering Model

| Layer | Responsibility | Components |
|:---|:---|:---|
| **User Layer** | Interaction & Logic | `Desktop (Shell)`, `Apps (Notepad, Settings)` |
| **Widget Layer** | UI Elements | `Button`, `Window`, `Label`, `Panel` |
| **Window Manager** | Composition & Input | `WindowManager`, `Compositor`, `EventLoop` |
| **Core Gfx** | Drawing & Clipping | `Canvas` (Rects, Text, Lines) w/ **Clipping** |
| **System Services** | Infrastructure | **`Heap Allocator`**, `Event Queue` |
| **Hardware** | Drivers | `VBE` (Display), `Mouse`, `Keyboard` |

### 1.2 Critical Infrastructure Needs
To support a scalable GUI, Phase 3 MUST introduce:
1.  **Dynamic Memory (Heap)**: We cannot statically allocate Windows and Widgets. A simple `kheap` (Next/Best Fit) is required.
2.  **Event Queue**: Polling hardware directly in apps is messy. We need `EventPush()` (ISR) -> `EventPop()` (Main Loop).

### 1.3 Execution Model (The "Game Loop")
The kernel `while(1)` loop becomes the **GUI Event Loop**:
1.  **Poll Input**: Fetch events from the queue (Mouse, Key, System).
2.  **Dispatch**: Route event to the **Active Window** (or global shell).
3.  **Update**: Run logic (animations, state changes).
4.  **Compose & Render**:
    *   Clear dirty regions.
    *   Draw Desktop background.
    *   Draw Windows (Back-to-Front) with **Clipping**.
    *   Draw Overlay (Cursor).

---

## 2. Window Management & Input

### 2.1 Window Concept
*   **Structure**: `Window` struct containing a list of `Widget` children.
*   **Capabilities**:
    *   `bounds` (x, y, w, h)
    *   `title`
    *   `flags` (MOVABLE, CLOSABLE, MODAL)
    *   `z_index` (Managed by WM list order)

### 2.2 Input Routing
*   **Focus**: One window is `Active`. It receives all Keyboard events.
*   **Mouse**:
    *   Click acts on `Z-Order` (hit test top-most window first).
    *   **Focus-follows-click**: Clicking a window brings it to front and makes it Active.
    *   **Drag**: Titlebar drag moves the window rect.

---

## 3. UX & UI Core Principles

### 3.1 Unifying Philosophy
*   **"Calm & Solid"**: No flashing, no jitter. UI elements have clear borders (1px black) and solid fills.
*   **Predictable**: Controls always look like controls.
*   **No "Eye Candy"**: No blurring, no transparency (alpha blending is expensive in software anyway), no drop shadows (unless optimizations allow simple hard shadows).

### 3.2 UI Layout (The "GemOS Look")
*   **Colors**: High-contrast, cool palette.
    *   Background: `#202020` (Dark Gray) or `#008080` (Classic Teal).
    *   Window BG: `#C0C0C0` (Silver) or `#E0E0E0` (Light Gray).
    *   Active Title: `#000080` (Navy). Inactive: `#808080`.
*   **Global Top Bar**:
    *   Fixed height (e.g., 24px).
    *   Left: `GemOS` Menu (System verbs: Shutdown, About).
    *   Center: Current App Name (optional).
    *   Right: Clock `HH:MM:SS`.
*   **Bottom Task Bar**:
    *   Lists open windows.
    *   Active window button is "pressed" (darker).

---

## 4. Phase 3 Roadmap

We will execute this phase in strict order. We cannot build windows without a heap.

### **Phase 3.1: Foundations**
*   **Memory**: Implement `kalloc` / `kfree` (Simple linked list heap).
*   **Events**: Implement `InputQueue` (Ring buffer for `Event` structs).
*   **Text**: Basic Bitmap Font rendering (hardcoded glyphs initially).

### **Phase 3.2: Graphics Core**
*   **GfxContext**: Abstraction over VBE.
*   **Clipping**: Implement `set_clip_rect(x, y, w, h)` to prevent drawing outside windows.
*   **Double Buffering (Optional but recommended)**: If RAM allows, render to backbuffer to avoid flicker. (Given 32-bit mode, we likely have RAM).

### **Phase 3.3: The Window Manager**
*   Define `Window` struct.
*   Implement `WM_AddWindow`, `WM_RemoveWindow`, `WM_BringToFront`.
*   Implement `WM_Draw()` (Painter's algorithm).
*   Implement **Dragging** logic.

### **Phase 3.4: Widget Framework**
*   Base `Widget` class.
*   `Button`, `Label` widgets.
*   Event propagation (`OnClick`).

### **Phase 3.5: The Shell**
*   Implement Top Bar.
*   Implement Task Bar.
*   Assemble the "Desktop" experience.
