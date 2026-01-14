# GemLang Specification

**Gem** is the native application language of GemOS. It is a high-level, declarative, interpreted language designed to make GUI application development trivial.

## 1. Philosophy: State is Truth
In traditional UI programming (Win32, GTK), you create a text field and manually update its content when data changes. In GemLang, you define **Data** and you bind the **UI** to it.
*   **Change the data** -> **The UI updates automatically.**

## 2. Syntax Overview

GemLang uses a block-based syntax familiar to users of C, Swift, or Kotlin.

```gem
App "AppName" {
    // Logic/State Definition
    var count = 0

    // UI Definition
    Window {
        title: "Counter"
        
        VStack {
            Label("Count: {count}")
            Button("Increment") {
                count = count + 1
            }
        }
    }
}
```

## 3. Key Concepts

### 3.1 Layouts
GemLang avoids absolute pixel positioning.
*   **VStack**: Vertical Stack. Arranges children top-to-bottom.
*   **HStack**: Horizontal Stack. Arranges children left-to-right.
*   **Stack Nesting**: You can nest stacks to create complex grids and layouts.

### 3.2 Components
*   **Label(text)**: Displays text. Supports variable interpolation (`{var}`).
*   **Button(text) { action }**: A clickable interactive element.
*   **TextField(variable)**: Planned. Two-way binding for input.
*   **Canvas { draw... }**: Canvas for custom drawing (games).

### 3.3 Logic
GemLang supports basic control flow within action blocks:
*   `if (condition) { ... }`
*   Variables: `var name = value`
*   Arithmetic: `+`, `-`, `*`, `/`

## 4. Runtime Integration
GemLang scripts reside in the `/apps` directory. The GemCore kernel includes a specialized interpreter that parses these scripts at runtime, constructing the native window structures they describe. This means GemLang apps are:
*   **Safe:** They cannot crash the kernel directly.
*   **Portable:** The script is the executable.
*   **Native:** They render using the kernel's high-speed graphics routines.
