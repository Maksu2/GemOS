#!/bin/bash
set -e

# Tools
CC=x86_64-elf-gcc
LD=x86_64-elf-ld

# Compile Bootloader
nasm src/boot/boot.asm -f bin -o build/boot.bin

# Compile Kernel Entry
nasm src/kernel/kernel_entry.asm -f elf -o build/kernel_entry.o

# Compile Interrupts ASM
nasm src/kernel/interrupts.asm -f elf -o build/interrupts.o

# Compile Kernel
$CC -m32 -ffreestanding -c src/kernel/kernel.c -o build/kernel.o
$CC -m32 -ffreestanding -c src/drivers/video.c -o build/video.o
$CC -m32 -ffreestanding -c src/kernel/idt.c -o build/idt.o
$CC -m32 -ffreestanding -c src/kernel/handlers.c -o build/handlers.o
$CC -m32 -ffreestanding -c src/kernel/window.c -o build/window.o
$CC -m32 -ffreestanding -c src/kernel/apps.c -o build/apps.o
$CC -m32 -ffreestanding -c src/kernel/gemlang.c -o build/gemlang.o

# Link Kernel
# We link to 0x1000 because bootloader loads us there.
# --oformat binary outputs raw machine code.
$LD -m elf_i386 -o build/kernel.bin -Ttext 0x10000 --oformat binary build/kernel_entry.o build/interrupts.o build/kernel.o build/idt.o build/handlers.o build/video.o build/window.o build/apps.o build/gemlang.o

# Create OS Image
cat build/boot.bin build/kernel.bin > build/os.img

# Padding (optional, to ensure we have enough sectors if we strictly read 50)
# boot.bin is 512 bytes.
# We need to make sure the file is large enough for the 'read 50 sectors' command to not fail/read garbage?
# Actually BIOS usually just reads available. But best to pad.
truncate -s 1M build/os.img

echo "Build Complete: build/os.img"
