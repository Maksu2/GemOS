# GemLang 2.0 Specification

**Version:** 2.0  
**Type:** Application Systems Language  
**Target:** CustomOS Native Ecosystem

---

## 1. Philosophy

GemLang 2.0 is designed to make application development **safe, predictable, and robust**. Unlike lower-level languages where the developer must manually manage pixels and memory, GemLang operates on the principle of **State-Driven Reactivity**.

### Core Tenets:
1.  **State is Truth**: The user interface is merely a projection of the application's current state. Changing a variable typically triggers a UI update.
2.  **Declarative Hierarchy**: Interfaces are defined as nested trees of components. The visual structure matches the code structure.
3.  **No Pixels (Unless You Want Them)**: Layout is handled by container stacks (`VStack`, `HStack`), ensuring applications adapt to different window sizes automatically.
4.  **Safe by Default**: The language prevents common errors like buffer overflows or null pointer dereferences by abstraction.

---

## 2. Application Structure

A GemLang application consists of a single `App` definition block that acts as the entry point.

```gem
App "MyApplication" {
    // 1. Logic & State
    var count = 0

    // 2. Interface Definition
    Window {
        title: "Counter"
        width: 300
        height: 200

        Body {
            // UI Content
        }
    }
}
```

### 2.1 File System Integration
Applications are compiled or interpreted from `.gem` files located in `/apps/`. The filename determines the application ID.
*   `/apps/notepad.gem`
*   `/apps/calc.gem`

---

## 3. State & Logic

GemLang separates logic from presentation but keeps them in the same scope for ease of use.

### 3.1 Variables
Variables are strictly typed but structurally inferred.
*   `var name = "User"` (String)
*   `var score = 100` (Int)
*   `var isActive = true` (Bool)

### 3.2 Logic & Events
Logic is executed inside **Event Blocks** (like `onClick`) or **Lifecycle Blocks** (like `onTick`).

Supported Control Flow:
*   `if (condition) { ... } else { ... }`
*   `while (condition) { ... }` (Looping)

### 3.3 Reactivity
When a variable used in the UI changes, the affected components redraw automatically.
*   `Text("Score: {score}")` -> Wraps `score` in a binding.

---

## 4. Layout System

GemLang 2.0 moves away from `x, y` coordinates.

### Containers
*   **VStack**: Stacks children vertically.
*   **HStack**: Stacks children horizontally.
*   **ZStack**: Stacks children on top of each other (depth).

### Modifiers
Components can be modified using dot syntax (SwiftUI style).
*   `.padding(10)`
*   `.background(Color.Red)`
*   `.frame(width: 100)`

---

## 5. Component Library

### 5.1 Inputs & Outputs
*   **Label(text)**: Read-only text.
*   **Button(label) { action }**: Clickable action.
*   **TextField(binding)**: Editable text input linked to a variable.

### 5.2 Containers
*   **Window**: Top-level frame.
*   **ScrollView**: Scrollable region.
*   **List**: Vertical repeatable list.

### 5.3 Graphics
*   **Canvas**: A region for immediate-mode drawing (lines, rects, pixels) for games.

---

## 6. Reference Examples

### Example 1: Scientific Calculator
Demonstrates Grid layout, State, and Logic.

```gem
App "SciCalc" {
    var display = "0"
    var acc = 0
    var op = ""

    Window {
        title: "Scientific Calculator"
        width: 300
        height: 400

        VStack {
            // Display Screen
            Label("{display}")
                .background(Color.Black)
                .color(Color.Green)
                .height(50)
                .align(Right)

            // Keypad
            HStack {
                Button("7") { display = display + "7" }
                Button("8") { display = display + "8" }
                Button("9") { display = display + "9" }
                Button("/") { op = "/" }
            }
            HStack {
                Button("4") { display = display + "4" }
                Button("5") { display = display + "5" }
                Button("6") { display = display + "6" }
                Button("*") { op = "*" }
            }
            HStack {
                Button("1") { display = display + "1" }
                Button("2") { display = display + "2" }
                Button("3") { display = display + "3" }
                Button("-") { op = "-" }
            }
            HStack {
                Button("0") { display = display + "0" }
                Button("C") { display = "0"; acc = 0 }
                Button("=") { 
                    // Calculate logic
                    if (op == "+") { display = acc + display } 
                }
                Button("+") { op = "+" }
            }
        }
    }
}
```

### Example 2: Simple Game (Pang)
Demonstrates Timer and Canvas.

```gem
App "PangGame" {
    var playerX = 100
    var ballX = 50
    var ballY = 50

    // Game Loop (60 FPS)
    onTick {
        ballX = ballX + 1
        if (ballX > 300) { ballX = 0 }
    }

    Window {
        title: "Pang"
        width: 400
        height: 300

        Canvas {
            drawRect(playerX, 280, 20, 20, Color.Blue)
            drawCircle(ballX, ballY, 10, Color.Red)
        }
        .onClick {
            // Move player
            playerX = playerX + 10
        }
    }
}
```

---

## 7. Migration Guide
Migrating from GemLang 1.0 to 2.0 requires rewriting the UI definition to use Stacks instead of coordinate placements. The logic model remains similar but gains `if/else` capabilities.

This specification serves as the blueprint for the CustomOS Application Ecosystem.

