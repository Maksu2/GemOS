# =============================================================================
# GemOS Makefile
# =============================================================================
# Prerequisites:
#   - nasm
#   - an i686-elf or x86_64-elf cross toolchain, or a host gcc/binutils that
#     accept -m32 / -m elf_i386 (fallback, used by CI)
#   - qemu-system-i386 (run, debug, tools/smoke.sh)
#
# Targets:
#   make all    - build build/gemos.img (boot floppy)
#   make run    - boot the floppy image in QEMU with the GemFS data disk
#   make run-hdd - boot the hard disk image instead (data disk second)
#   make debug  - same as run, paused, with a GDB stub on :1234
#   make clean  - remove build/
#   make info   - show the toolchain and the object list
#
# Smoke test: tools/smoke.sh
# =============================================================================

# Toolchain: prefer an i686-elf cross compiler, then x86_64-elf with -m32,
# then the host toolchain with -m32.
ifneq ($(shell which i686-elf-gcc 2>/dev/null),)
    CC := i686-elf-gcc
    LD := i686-elf-ld
    OBJCOPY := i686-elf-objcopy
    CROSS_COMPILE := 1
else ifneq ($(shell which x86_64-elf-gcc 2>/dev/null),)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
    OBJCOPY := x86_64-elf-objcopy
    CROSS_COMPILE := 1
else
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
OBJ_DIR := $(BUILD_DIR)/obj
BOOT_DIR := boot
USERLAND_DIR := userland

# Compiler flags
CFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector
CFLAGS += -nostdlib -nostdinc -fno-builtin
# No unwinder in the kernel or userland: do not emit .eh_frame
CFLAGS += -fno-asynchronous-unwind-tables
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -O2 -g
CFLAGS += -I. -Iinclude
CFLAGS += -mno-sse -mno-sse2 -mno-mmx
USERLAND_CFLAGS := $(filter-out -g -O2,$(CFLAGS)) -Os
DEPFLAGS := -MMD -MP

# Linker flags
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib
USER_LDSCRIPT := $(USERLAND_DIR)/user_linker.ld

# Output files
BOOT_STAGE1 := $(BUILD_DIR)/boot.bin
BOOT_STAGE2 := $(BUILD_DIR)/loader.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
OS_IMAGE := $(BUILD_DIR)/gemos.img
HDD_IMAGE := $(BUILD_DIR)/gemos-hdd.img
DATA_IMAGE := $(BUILD_DIR)/data.img

# =============================================================================
# Sources
# =============================================================================
# The order below is the link order. entry.S must stay first: stage 2 jumps
# to the first byte of the kernel image (0x100000), which must be _start.
KERNEL_ASM_SOURCES := kernel/entry.S kernel/interrupts.S \
                      kernel/context_switch.S kernel/gdt_flush.S

KERNEL_C_SOURCES := kernel/kernel.c kernel/console.c kernel/gdt.c kernel/idt.c \
                    kernel/fpu.c \
                    kernel/isr.c kernel/scheduler.c kernel/process.c kernel/elf.c \
                    kernel/syscall.c kernel/heap.c \
                    kernel/event.c kernel/memory/paging.c kernel/memory/pmm.c \
                    kernel/gfx/rect.c kernel/gfx/context.c kernel/gfx/primitives.c \
                    kernel/gfx/icons.c \
                    kernel/gfx/font/font.c \
                    kernel/gui/desktop.c kernel/gui/window/window.c \
                    kernel/gui/wm/wm.c kernel/gui/topbar/topbar.c \
                    kernel/ui/ui_scale.c kernel/ui/menu.c kernel/ui/dock/dock.c \
                    kernel/ui/cursor.c kernel/ui/focus.c \
                    kernel/app/app_manager.c apps/testapp/testapp.c \
                    apps/about/uabout_launcher.c \
                    apps/terminal/terminal.c apps/terminal/uterm_launcher.c \
                    apps/textedit/textedit.c apps/textedit/inputbox.c \
                    apps/textedit/filepicker.c apps/textedit/utextedit_launcher.c \
                    apps/explorer/explorer.c \
                    kernel/fs/gemfs.c \
                    kernel/font/aa.c kernel/font/truetype.c kernel/font/scanline.c \
                    kernel/font/font_cache.c

DRIVER_SOURCES := drivers/serial.c drivers/vbe.c drivers/pic.c drivers/pit.c \
                  drivers/keyboard.c drivers/mouse.c drivers/ata.c drivers/rtc.c

LIB_SOURCES := lib/string.c

# Code that runs in interrupt handlers must not touch the FPU: the FPU
# state of the interrupted task is not saved on interrupt entry.
IRQ_PATH_SOURCES := kernel/isr.c kernel/scheduler.c kernel/event.c \
                    drivers/pit.c drivers/pic.c drivers/keyboard.c \
                    drivers/mouse.c drivers/serial.c

# Userland programs embedded into the kernel image
USER_CRT0_SOURCE := $(USERLAND_DIR)/crt0.S
USRSMOKE_SOURCE := $(USERLAND_DIR)/usrsmoke.S
UTERM_SOURCES := $(USERLAND_DIR)/uterm2/main.c \
                 $(USERLAND_DIR)/uterm2/term_model.c \
                 $(USERLAND_DIR)/uterm2/term_input.c \
                 $(USERLAND_DIR)/uterm2/term_commands.c \
                 $(USERLAND_DIR)/uterm2/term_render.c
ABOUT_SOURCES := $(USERLAND_DIR)/about/main.c \
                 $(USERLAND_DIR)/about/about_state.c \
                 $(USERLAND_DIR)/about/about_render.c
UTEXTEDIT_SOURCES := $(USERLAND_DIR)/textedit/main.c \
                     $(USERLAND_DIR)/textedit/textedit_document.c \
                     $(USERLAND_DIR)/textedit/textedit_state.c \
                     $(USERLAND_DIR)/textedit/textedit_render.c

# =============================================================================
# Objects (build/obj mirrors the source tree)
# =============================================================================
obj = $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(basename $(1))))

KERNEL_OBJS := $(call obj,$(KERNEL_ASM_SOURCES) $(KERNEL_C_SOURCES) \
                          $(DRIVER_SOURCES) $(LIB_SOURCES))

USER_CRT0_OBJ := $(call obj,$(USER_CRT0_SOURCE))
USRSMOKE_OBJ := $(call obj,$(USRSMOKE_SOURCE))
UTERM_OBJS := $(call obj,$(UTERM_SOURCES))
ABOUT_OBJS := $(call obj,$(ABOUT_SOURCES))
UTEXTEDIT_OBJS := $(call obj,$(UTEXTEDIT_SOURCES))
USER_OBJS := $(USER_CRT0_OBJ) $(USRSMOKE_OBJ) $(UTERM_OBJS) $(ABOUT_OBJS) \
             $(UTEXTEDIT_OBJS)

# Binary blobs linked into the kernel. objcopy derives the symbol names from
# the input path (e.g. _binary_build_uterm_image_bin_start), and
# kernel/kernel.c and kernel/process.c refer to those names.
FONT_BLOB := $(OBJ_DIR)/blobs/font.ttf.o
USER_BLOBS := $(OBJ_DIR)/blobs/usrsmoke.elf.o \
              $(OBJ_DIR)/blobs/uterm_image.bin.o \
              $(OBJ_DIR)/blobs/about_image.bin.o \
              $(OBJ_DIR)/blobs/utextedit_image.bin.o
BLOB_OBJS := $(FONT_BLOB) $(USER_BLOBS)

DEPS := $(KERNEL_OBJS:.o=.d) $(USER_OBJS:.o=.d)

$(call obj,$(IRQ_PATH_SOURCES)): CFLAGS += -mgeneral-regs-only

# =============================================================================
# Targets
# =============================================================================

.PHONY: all clean run run-hdd debug info

# Keep intermediate files (e.g. build/uterm_image.bin) instead of deleting them
.SECONDARY:

all: $(OS_IMAGE) $(HDD_IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Stage 1 and stage 2 bootloader
$(BOOT_STAGE1): $(BOOT_DIR)/stage1/boot.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

$(BOOT_STAGE2): $(BOOT_DIR)/stage2/loader.asm | $(BUILD_DIR)
	$(AS) -f bin $< -o $@

# Objects also depend on the Makefile, so changing flags rebuilds them.
# Userland objects (listed first: GNU make 3.81 picks the first matching rule)
$(OBJ_DIR)/$(USERLAND_DIR)/%.o: $(USERLAND_DIR)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) $(USERLAND_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/$(USERLAND_DIR)/%.o: $(USERLAND_DIR)/%.S Makefile
	@mkdir -p $(@D)
	$(CC) $(USERLAND_CFLAGS) $(DEPFLAGS) -c $< -o $@

# Kernel, driver and library objects
$(OBJ_DIR)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.S Makefile
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Userland programs. -n: no page padding in the file. The data segment
# starts on its own page in memory, but in the file it follows the code
# directly (the loader copies each segment to its address), so programs
# stay within the 8 KB limit.
define link-user
	$(LD) -m elf_i386 -n -T $(USER_LDSCRIPT) -nostdlib $(filter %.o,$^) -o $@
	$(OBJCOPY) --strip-all $@
endef

$(BUILD_DIR)/usrsmoke.elf: $(USRSMOKE_OBJ) $(USER_LDSCRIPT)
	$(link-user)

$(BUILD_DIR)/uterm.elf: $(USER_CRT0_OBJ) $(UTERM_OBJS) $(USER_LDSCRIPT)
	$(link-user)

$(BUILD_DIR)/about.elf: $(USER_CRT0_OBJ) $(ABOUT_OBJS) $(USER_LDSCRIPT)
	$(link-user)

$(BUILD_DIR)/utextedit.elf: $(USER_CRT0_OBJ) $(UTEXTEDIT_OBJS) $(USER_LDSCRIPT)
	$(link-user)

$(BUILD_DIR)/%_image.bin: $(BUILD_DIR)/%.elf
	cp $< $@

# Blobs. The font is converted from inside assets/ so its symbols stay
# _binary_font_ttf_start/_end.
$(FONT_BLOB): assets/font.ttf
	@mkdir -p $(@D)
	cd $(<D) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 $(<F) $(abspath $@)

$(OBJ_DIR)/blobs/%.o: $(BUILD_DIR)/%
	@mkdir -p $(@D)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

# Kernel
$(KERNEL_ELF): $(KERNEL_OBJS) $(BLOB_OBJS) linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) $(BLOB_OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# Boot floppy (1.44 MB), always written from scratch so no sectors from an
# older build survive behind the kernel.
# Layout:
#   Sector 0:     Stage 1 (512 bytes)
#   Sectors 1-32: Stage 2 (16KB)
#   Sectors 33+:  Kernel; stage 2 reads its size from the header at offset 4
#                 of kernel.bin ("GEMK" + byte count, kernel/entry.S)
FLOPPY_SECTORS := 2880
KERNEL_START_SECTOR := $(shell sed -n 's/^KERNEL_START_SECTOR[[:space:]]*equ[[:space:]]*\([0-9][0-9]*\).*/\1/p' $(BOOT_DIR)/stage2/loader.asm)
ifeq ($(KERNEL_START_SECTOR),)
    $(error Cannot read KERNEL_START_SECTOR from $(BOOT_DIR)/stage2/loader.asm)
endif

# kernel.bin must start with the header and fit on the floppy
define check-kernel-image
	@size=$$(wc -c < $(KERNEL_BIN)); \
	magic=$$(od -An -c -j4 -N4 $(KERNEL_BIN) | tr -d ' '); \
	header=$$(od -An -tu4 -j8 -N4 $(KERNEL_BIN) | tr -d ' '); \
	if [ "$$magic" != "GEMK" ] || [ "$$header" != "$$size" ]; then \
	  echo "error: $(KERNEL_BIN) header says '$$magic' $$header bytes, the file has $$size bytes." >&2; \
	  echo "       kernel/entry.S must be linked first and linker.ld must end the image after .data." >&2; \
	  exit 1; \
	fi; \
	max=$$(( ($(FLOPPY_SECTORS) - $(KERNEL_START_SECTOR)) * 512 )); \
	if [ $$size -gt $$max ]; then \
	  echo "error: $(KERNEL_BIN) is $$size bytes, the boot floppy has room for $$max." >&2; \
	  exit 1; \
	fi
endef

$(OS_IMAGE): $(BOOT_STAGE1) $(BOOT_STAGE2) $(KERNEL_BIN)
	$(check-kernel-image)
	@echo "Creating disk image..."
	@rm -f $@ $@.tmp
	dd if=/dev/zero of=$@.tmp bs=512 count=$(FLOPPY_SECTORS) 2>/dev/null
	dd if=$(BOOT_STAGE1) of=$@.tmp conv=notrunc bs=512 count=1 2>/dev/null
	dd if=$(BOOT_STAGE2) of=$@.tmp conv=notrunc bs=512 seek=1 2>/dev/null
	dd if=$(KERNEL_BIN) of=$@.tmp conv=notrunc bs=512 seek=$(KERNEL_START_SECTOR) 2>/dev/null
	@mv $@.tmp $@
	@echo "Disk image created: $@"
	@echo "  Stage 1: $$(wc -c < $(BOOT_STAGE1) | tr -d ' ') bytes"
	@echo "  Stage 2: $$(wc -c < $(BOOT_STAGE2) | tr -d ' ') bytes"
	@echo "  Kernel:  $$(wc -c < $(KERNEL_BIN) | tr -d ' ') bytes"

# Bootable hard disk image (16 MB) with the floppy's layout: stage 1 in the
# MBR, stage 2 in sectors 1-32, the kernel from sector 33. Both stages read
# it with the INT 13h extensions (LBA). GemFS never uses a disk with a boot
# signature, so the data disk goes second.
HDD_SECTORS := 32768

$(HDD_IMAGE): $(BOOT_STAGE1) $(BOOT_STAGE2) $(KERNEL_BIN)
	$(check-kernel-image)
	@rm -f $@ $@.tmp
	dd if=/dev/zero of=$@.tmp bs=512 count=$(HDD_SECTORS) 2>/dev/null
	dd if=$(BOOT_STAGE1) of=$@.tmp conv=notrunc bs=512 count=1 2>/dev/null
	dd if=$(BOOT_STAGE2) of=$@.tmp conv=notrunc bs=512 seek=1 2>/dev/null
	dd if=$(KERNEL_BIN) of=$@.tmp conv=notrunc bs=512 seek=$(KERNEL_START_SECTOR) 2>/dev/null
	@mv $@.tmp $@
	@echo "Hard disk image created: $@"

# GemFS data disk (10MB). Created once and kept between runs.
$(DATA_IMAGE): | $(BUILD_DIR)
	@echo "Creating data image..."
	dd if=/dev/zero of=$@ bs=1M count=10 2>/dev/null

# Run in QEMU
run: $(OS_IMAGE) $(DATA_IMAGE)
	$(QEMU) -fda $(OS_IMAGE) -hda $(DATA_IMAGE) -serial stdio -m 128M

run-hdd: $(HDD_IMAGE) $(DATA_IMAGE)
	$(QEMU) -drive file=$(HDD_IMAGE),if=ide,index=0,format=raw \
	  -drive file=$(DATA_IMAGE),if=ide,index=1,format=raw -boot c \
	  -serial stdio -m 128M

# Debug mode (pause at start, enable GDB). Without the data disk the system
# starts too, just without a file system.
debug: $(OS_IMAGE) $(DATA_IMAGE)
	$(QEMU) -fda $(OS_IMAGE) -hda $(DATA_IMAGE) -serial stdio -m 128M -S -s

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
	@echo "  $(KERNEL_OBJS) $(BLOB_OBJS)"

-include $(DEPS)
