# GemOS

A modern, minimalist desktop operating system built from scratch.

## Philosophy

GemOS is a **classic desktop system** (WIMP) that is:
- Designed consciously as a product, not a demo
- Consistent from bootloader to GUI
- Calm, predictable, and aesthetic

## Requirements

### Build Tools
- `nasm` - Netwide Assembler
- `gcc` with 32-bit support (or `i686-elf-gcc` cross-compiler)
- `ld` (part of binutils)
- `make`

### Runtime
- `qemu-system-i386` (for testing)

### macOS Installation
```bash
brew install nasm qemu
# For cross-compiler (optional but recommended):
brew install i686-elf-gcc i686-elf-binutils
```

### Linux Installation
```bash
# Debian/Ubuntu
sudo apt install nasm gcc-multilib qemu-system-x86

# Arch
sudo pacman -S nasm qemu gcc
```

## Building

```bash
make all      # Build everything
make run      # Build and run in QEMU
make clean    # Remove build artifacts
make debug    # Run with GDB support (connect with 'target remote :1234')
```

## Project Structure

```
GemOS-new/
├── boot/               # Bootloader
│   ├── stage1/         # MBR (512 bytes)
│   └── stage2/         # Protected mode entry
├── kernel/             # Kernel core
├── drivers/            # Hardware drivers
├── lib/                # System libraries
├── gui/                # GUI subsystem (future)
├── apps/               # Applications (future)
├── Makefile
├── linker.ld
└── README.md
```

## Architecture

- **Target**: x86, 32-bit Protected Mode
- **Graphics**: VBE (VESA BIOS Extensions)
- **No dependencies**: Everything from scratch, no libc, no GRUB

## Current Status

### Phase 1: Boot to Protected Mode ✓
- [x] Stage 1 bootloader (MBR)
- [x] Stage 2 loader (A20, Protected Mode)
- [x] VBE graphics mode setup
- [x] Kernel entry point
- [x] Serial debug output
- [x] Basic VBE driver

### Coming Next
- Phase 2: Interrupts and Timer
- Phase 3: Memory Management
- Phase 4: Input Devices
- ...and more

## License

MIT License
