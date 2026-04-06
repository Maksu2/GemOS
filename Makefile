# =============================================================================
# GemOS Makefile
# =============================================================================
# Build system for GemOS kernel and bootloader.
# 
# Prerequisites:
#   - nasm (assembler)
#   - i686-elf-gcc (cross-compiler) or gcc with -m32
#   - i686-elf-ld (linker) or ld with appropriate flags
#   - qemu-system-i386
#
# Targets:
#   make all    - Build everything
#   make run    - Build and run in QEMU
#   make clean  - Remove build artifacts
#   make debug  - Run with QEMU debug options
# =============================================================================

# Toolchain
# Try to use i686-elf cross-compiler, then x86_64-elf with -m32
ifneq ($(shell which i686-elf-gcc 2>/dev/null),)
    # i686-elf cross-compiler available
    CC := i686-elf-gcc
    LD := i686-elf-ld
    OBJCOPY := i686-elf-objcopy
    CROSS_COMPILE := 1
else ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
    # x86_64-elf cross-compiler available (use with -m32)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
    OBJCOPY := x86_64-elf-objcopy
    CROSS_COMPILE := 1
else
    # No cross-compiler, this likely won't work on non-x86 hosts
    $(warning No x86 cross-compiler found. Please install i686-elf-gcc or x86_64-elf-gcc)
    CC := gcc
    LD := ld
    OBJCOPY := objcopy
    CROSS_COMPILE := 0
endif

AS := nasm
QEMU := $(shell which qemu-system-i386 2>/dev/null || echo /opt/homebrew/bin/qemu-system-i386)

# Directories
BUILD_DIR := build
BOOT_DIR := boot
KERNEL_DIR := kernel
DRIVERS_DIR := drivers
LIB_DIR := lib
USERLAND_DIR := userland

# Compiler flags
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector
CFLAGS += -nostdlib -nostdinc -fno-builtin
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -O2 -g
CFLAGS += -I. -Iinclude
CFLAGS += -mno-sse -mno-sse2 -mno-mmx
USERLAND_CFLAGS := $(filter-out -g,$(CFLAGS))

# Linker flags
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# Assembler flags
ASFLAGS := -f elf32

# Output files
BOOT_STAGE1 := $(BUILD_DIR)/boot.bin
BOOT_STAGE2 := $(BUILD_DIR)/loader.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
OS_IMAGE := $(BUILD_DIR)/gemos.img
USER_SMOKE_OBJ := $(BUILD_DIR)/usrsmoke_user.o
USER_SMOKE_ELF := $(BUILD_DIR)/usrsmoke.elf
USER_SMOKE_BLOB := $(BUILD_DIR)/usrsmoke_blob.o
UTERM_CRT_OBJ := $(BUILD_DIR)/uterm_crt0.o
UTERM_OBJ := $(BUILD_DIR)/uterm_user.o
UTERM_ELF := $(BUILD_DIR)/uterm.elf
UTERM_BLOB := $(BUILD_DIR)/uterm_blob.o

# Source files
# Source files
KERNEL_SOURCES := $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/console.c $(KERNEL_DIR)/gdt.c $(KERNEL_DIR)/idt.c $(KERNEL_DIR)/isr.c $(KERNEL_DIR)/scheduler.c $(KERNEL_DIR)/process.c $(KERNEL_DIR)/elf.c $(KERNEL_DIR)/syscall.c $(KERNEL_DIR)/tests.c tests/visual_test.c tests/window_test.c tests/font_test.c $(KERNEL_DIR)/heap.c $(KERNEL_DIR)/event.c $(KERNEL_DIR)/memory/paging.c \
                  $(KERNEL_DIR)/gfx/rect.c $(KERNEL_DIR)/gfx/context.c $(KERNEL_DIR)/gfx/primitives.c $(KERNEL_DIR)/gfx/font/font.c $(KERNEL_DIR)/gfx/font/glyphs.c $(KERNEL_DIR)/gui/desktop.c \
                  $(KERNEL_DIR)/gui/window/window.c $(KERNEL_DIR)/gui/wm/wm.c $(KERNEL_DIR)/gui/topbar/topbar.c \
                  $(KERNEL_DIR)/ui/ui_scale.c $(KERNEL_DIR)/ui/menu.c $(KERNEL_DIR)/ui/dock/dock.c $(KERNEL_DIR)/ui/cursor.c $(KERNEL_DIR)/ui/focus.c \
                  $(KERNEL_DIR)/app/app_manager.c apps/testapp/testapp.c apps/about/about.c apps/terminal/terminal.c apps/terminal/uterm_launcher.c \
                  apps/textedit/textedit.c apps/textedit/inputbox.c apps/textedit/filepicker.c \
                  apps/explorer/explorer.c \
                  $(KERNEL_DIR)/fs/gemfs.c \
                  $(KERNEL_DIR)/gfx/blur.c \
                  $(KERNEL_DIR)/font/glyphs_sys.c $(KERNEL_DIR)/font/rasterizer.c $(KERNEL_DIR)/font/aa.c $(KERNEL_DIR)/font/truetype.c $(KERNEL_DIR)/font/scanline.c $(KERNEL_DIR)/font/font_cache.c

# ... later in file ...

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/ui/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
DRIVER_SOURCES := $(DRIVERS_DIR)/serial.c $(DRIVERS_DIR)/vbe.c $(DRIVERS_DIR)/pic.c $(DRIVERS_DIR)/pit.c $(DRIVERS_DIR)/keyboard.c $(DRIVERS_DIR)/mouse.c $(DRIVERS_DIR)/ata.c $(DRIVERS_DIR)/rtc.c
LIB_SOURCES := $(LIB_DIR)/string.c
ASM_SOURCES := $(KERNEL_DIR)/entry.S $(KERNEL_DIR)/interrupts.S $(KERNEL_DIR)/context_switch.S $(KERNEL_DIR)/gdt_flush.S

# Object files
KERNEL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(KERNEL_SOURCES)))
DRIVER_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(DRIVER_SOURCES)))
LIB_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(LIB_SOURCES)))
ASM_OBJS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(notdir $(ASM_SOURCES)))

ALL_OBJS := $(ASM_OBJS) $(KERNEL_OBJS) $(DRIVER_OBJS) $(LIB_OBJS) $(BUILD_DIR)/font_data.o $(USER_SMOKE_BLOB) $(UTERM_BLOB)

# =============================================================================
# Targets
# =============================================================================

.PHONY: all clean run debug

all: $(OS_IMAGE)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Stage 1 bootloader
$(BOOT_STAGE1): $(BOOT_DIR)/stage1/boot.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

# Stage 2 bootloader
$(BOOT_STAGE2): $(BOOT_DIR)/stage2/loader.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

# Generic rule for C files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/memory/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gui/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gfx/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gfx/font/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gui/window/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gui/wm/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(DRIVERS_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(LIB_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/gui/topbar/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/ui/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/ui/dock/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/app/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: apps/testapp/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: apps/about/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: apps/terminal/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: apps/textedit/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: apps/explorer/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Generic rule for Test files
$(BUILD_DIR)/%.o: tests/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Generic rule for ASM files
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_SMOKE_OBJ): $(USERLAND_DIR)/usrsmoke.S | $(BUILD_DIR)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(USER_SMOKE_ELF): $(USER_SMOKE_OBJ) $(USERLAND_DIR)/user_linker.ld
	$(LD) -m elf_i386 -T $(USERLAND_DIR)/user_linker.ld -nostdlib $(USER_SMOKE_OBJ) -o $@
	$(OBJCOPY) --strip-all $@

$(USER_SMOKE_BLOB): $(USER_SMOKE_ELF)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

$(UTERM_CRT_OBJ): $(USERLAND_DIR)/crt0.S | $(BUILD_DIR)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(UTERM_OBJ): $(USERLAND_DIR)/uterm.c | $(BUILD_DIR)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(UTERM_ELF): $(UTERM_CRT_OBJ) $(UTERM_OBJ) $(USERLAND_DIR)/user_linker.ld
	$(LD) -m elf_i386 -T $(USERLAND_DIR)/user_linker.ld -nostdlib $(UTERM_CRT_OBJ) $(UTERM_OBJ) -o $@
	$(OBJCOPY) --strip-all $@

$(UTERM_BLOB): $(UTERM_ELF)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/font/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/fs/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Font Data (linked binary)
$(BUILD_DIR)/font_data.o: font.ttf
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 font.ttf $@

# Link kernel
$(KERNEL_ELF): $(ALL_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(ALL_OBJS) -o $@

# Convert ELF to raw binary
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# Create disk image
# Layout:
#   Sector 0: Stage 1 (512 bytes)
#   Sectors 1-32: Stage 2 (16KB)
#   Sectors 33+: Kernel
$(OS_IMAGE): $(BOOT_STAGE1) $(BOOT_STAGE2) $(KERNEL_BIN)
	@echo "Creating disk image..."
	# Create empty 1.44MB floppy image IF IT DOES NOT EXIST
	if [ ! -f $@ ]; then dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null; fi
	# Write Stage 1 (MBR)
	dd if=$(BOOT_STAGE1) of=$@ conv=notrunc bs=512 count=1 2>/dev/null
	# Write Stage 2
	dd if=$(BOOT_STAGE2) of=$@ conv=notrunc bs=512 seek=1 2>/dev/null
	# Write kernel
	dd if=$(KERNEL_BIN) of=$@ conv=notrunc bs=512 seek=33 2>/dev/null
	@echo "Disk image updated: $@"
	@echo "  Stage 1: 512 bytes"
	@echo "  Stage 2: $$(stat -f%z $(BOOT_STAGE2) 2>/dev/null || stat -c%s $(BOOT_STAGE2)) bytes"
	@echo "  Kernel:  $$(stat -f%z $(KERNEL_BIN) 2>/dev/null || stat -c%s $(KERNEL_BIN)) bytes"


DATA_IMAGE := $(BUILD_DIR)/data.img

# Create data image (10MB) if not exists
$(DATA_IMAGE):
	@echo "Creating data image..."
	if [ ! -f $@ ]; then dd if=/dev/zero of=$@ bs=1M count=10 2>/dev/null; fi

# Run in QEMU
run: $(OS_IMAGE) $(DATA_IMAGE)
	$(QEMU) -fda $(OS_IMAGE) -hda $(DATA_IMAGE) -serial stdio -m 128M

# Debug mode (pause at start, enable GDB)
debug: $(OS_IMAGE)
	$(QEMU) -fda $(OS_IMAGE) -serial stdio -m 128M -S -s

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# Show info
info:
	@echo "Toolchain:"
	@echo "  CC = $(CC)"
	@echo "  LD = $(LD)"
	@echo "  AS = $(AS)"
	@echo "  Cross-compile = $(CROSS_COMPILE)"
	@echo ""
	@echo "Objects:"
	@echo "  $(ALL_OBJS)"

# Run with VNC Bridge (Visual Agent)
# Usage: make run-bridge
# Then open http://localhost:6080/vnc.html
run-bridge: $(OS_IMAGE)
	@echo "Starting VNC Bridge Mode..."
	$(QEMU) -fda $(OS_IMAGE) -vnc :0 -serial stdio -m 128M & \
	./tools/noVNC/utils/novnc_proxy --vnc localhost:5900 --listen 6080
