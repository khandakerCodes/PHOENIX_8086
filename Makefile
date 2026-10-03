# SPDX-License-Identifier: MIT
# ============================================================================
# Phoenix-8086 — Master Makefile
# ============================================================================
#
# Builds the entire system: bootloaders, kernel, and disk image.
#
# Targets:
#   make all       — Build everything and create disk image
#   make toolchain — Fetch the ia16-elf toolchain into .toolchain/
#   make boot      — Assemble bootloaders
#   make kernel    — Compile kernel
#   make image     — Create bootable floppy image
#   make check     — Verify the kernel uses 8086 instructions only
#   make test      — check + headless boot and shell integration tests in QEMU
#   make run       — Build and run in QEMU
#   make debug     — Run in QEMU with GDB server
#   make dashboard — Serve the visual dashboard
#   make clean     — Remove all build artifacts
#
# ============================================================================

# ── Toolchain ───────────────────────────────────
# The kernel is built with ia16-elf-gcc (true 16-bit, 8086 code).
# A project-local copy in .toolchain/ takes precedence over PATH.
ifneq ($(wildcard .toolchain/usr/bin/ia16-elf-gcc),)
CROSS   = $(CURDIR)/.toolchain/usr/bin/ia16-elf-
# The unpacked binutils look for their shared libraries under /usr
export LD_LIBRARY_PATH := $(CURDIR)/.toolchain/usr/x86_64-linux-gnu/ia16-elf/lib:$(LD_LIBRARY_PATH)
else
CROSS   = ia16-elf-
endif

NASM    = nasm
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
PYTHON  = python3
QEMU    = qemu-system-i386

# ── Options ─────────────────────────────────────
# TELEMETRY=0 compiles the telemetry system out (run 'make clean' when switching)
TELEMETRY ?= 1

# ── Flags ───────────────────────────────────────
ARCHFLAGS  = -march=i8086 -mtune=i8086 -mcmodel=small
NASMFLAGS  = -f bin
CFLAGS     = $(ARCHFLAGS) -ffreestanding -fno-builtin \
             -fno-delete-null-pointer-checks \
             -DCONFIG_TELEMETRY=$(TELEMETRY) \
             -Wall -Wextra -Os -MMD -MP -c
ASFLAGS    = $(ARCHFLAGS) -MMD -MP -c
LDFLAGS    = -T linker/kernel.ld --no-check-sections

# ── Directories ─────────────────────────────────
BUILD_DIR  = build
BOOT_DIR   = boot
KERNEL_DIR = kernel
INC_DIR    = include

# ── Source files ────────────────────────────────
BOOT_STAGE1 = $(BOOT_DIR)/stage1.asm
BOOT_STAGE2 = $(BOOT_DIR)/stage2.asm

# GAS assembly sources (.S), 8086 instructions only
KERNEL_S_SRCS = $(KERNEL_DIR)/entry.S \
                $(KERNEL_DIR)/isr.S \
                $(KERNEL_DIR)/hal.S

KERNEL_C_SRCS = $(KERNEL_DIR)/kernel_main.c \
                $(KERNEL_DIR)/console.c \
                $(KERNEL_DIR)/interrupts.c \
                $(KERNEL_DIR)/keyboard.c \
                $(KERNEL_DIR)/thread.c \
                $(KERNEL_DIR)/scheduler.c \
                $(KERNEL_DIR)/memory.c \
                $(KERNEL_DIR)/syscall.c \
                $(KERNEL_DIR)/sync.c \
                $(KERNEL_DIR)/ipc.c \
                $(KERNEL_DIR)/shell.c \
                $(KERNEL_DIR)/panic.c \
                $(KERNEL_DIR)/debug.c \
                $(KERNEL_DIR)/stats.c \
                $(KERNEL_DIR)/idle.c \
                $(KERNEL_DIR)/selftest.c \
                $(KERNEL_DIR)/serial.c

ifeq ($(TELEMETRY),1)
KERNEL_C_SRCS += $(KERNEL_DIR)/telemetry.c
endif

# ── Object files ────────────────────────────────
KERNEL_S_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS))
KERNEL_C_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_OBJS   = $(KERNEL_S_OBJS) $(KERNEL_C_OBJS)

# ── Output files ────────────────────────────────
STAGE1_BIN = $(BUILD_DIR)/stage1.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
FLOPPY_IMG = $(BUILD_DIR)/phoenix8086.img

# ── Phony targets ──────────────────────────────
.PHONY: all toolchain boot kernel image check test run debug dashboard clean

# ── Default target ──────────────────────────────
all: image

# ── Fetch the cross toolchain ───────────────────
toolchain:
	./tools/get-toolchain.sh

# ── Create build directory ──────────────────────
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ── Stage 1 Bootloader ─────────────────────────
boot: $(STAGE1_BIN) $(STAGE2_BIN)

$(STAGE1_BIN): $(BOOT_STAGE1) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(STAGE2_BIN): $(BOOT_STAGE2) | $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

# ── Kernel GAS Assembly Objects (.S) ────────────
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	@command -v $(CC) >/dev/null || { echo "ERROR: $(CC) not found. Run 'make toolchain'."; exit 1; }
	$(CC) $(ASFLAGS) -I$(INC_DIR) -I$(KERNEL_DIR) -o $@ $<

# ── Kernel C Objects ────────────────────────────
$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	@command -v $(CC) >/dev/null || { echo "ERROR: $(CC) not found. Run 'make toolchain'."; exit 1; }
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(KERNEL_DIR) -o $@ $<

# ── Link Kernel ─────────────────────────────────
kernel: $(KERNEL_BIN)

# libgcc provides the 32-bit arithmetic helpers
$(KERNEL_ELF): $(KERNEL_OBJS) linker/kernel.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS) $$($(CC) $(ARCHFLAGS) -print-libgcc-file-name)

# Flatten by load address, then validate the header and patch the checksum
$(KERNEL_BIN): $(KERNEL_ELF) tools/mkimage.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) tools/mkimage.py $@

# ── Create Floppy Image ────────────────────────
image: $(FLOPPY_IMG)

$(FLOPPY_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) | $(BUILD_DIR)
	@echo "=== Creating floppy image ==="
	# Create a blank 1.44 MB floppy image
	dd if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null
	# Write Stage 1 bootloader to sector 1
	dd if=$(STAGE1_BIN) of=$@ bs=512 count=1 conv=notrunc 2>/dev/null
	# Write Stage 2 to sectors 2–5 (offset 512)
	dd if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	# Write kernel to sectors 6+ (offset 2560)
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=5 conv=notrunc 2>/dev/null
	@echo "=== Image created: $@ ==="
	@echo "  Stage 1: $$(wc -c < $(STAGE1_BIN)) bytes"
	@echo "  Stage 2: $$(wc -c < $(STAGE2_BIN)) bytes"
	@echo "  Kernel:  $$(wc -c < $(KERNEL_BIN)) bytes"

# ── Verify 8086-only instructions ──────────────
check: $(KERNEL_ELF)
	$(PYTHON) tools/check8086.py $(OBJDUMP) $(KERNEL_ELF)

# ── Automated tests ─────────────────────────────
test: check $(FLOPPY_IMG)
	$(PYTHON) -m unittest discover -q -b -s bridge -t .
	@if command -v node >/dev/null; then node --test dashboard/test/; \
	 else echo "  node not found: skipping dashboard model tests"; fi
	$(PYTHON) tools/smoke_test.py $(FLOPPY_IMG)
	$(PYTHON) tools/integration_test.py $(FLOPPY_IMG)

# ── Run in QEMU ─────────────────────────────────
run: $(FLOPPY_IMG)
	$(QEMU) -drive file=$(FLOPPY_IMG),format=raw,if=floppy \
	        -boot a \
	        -serial stdio \
	        -m 1M \
	        -display gtk

# ── Run with GDB debug server ──────────────────
debug: $(FLOPPY_IMG)
	$(QEMU) -drive file=$(FLOPPY_IMG),format=raw,if=floppy \
	        -boot a \
	        -serial stdio \
	        -m 1M \
	        -s -S \
	        -display gtk

# ── Serve the dashboard ────────────────────────
dashboard:
	@echo "Starting dashboard at http://localhost:8080"
	cd dashboard && python3 -m http.server 8080

# ── Clean ───────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	@echo "Build artifacts cleaned."

# ── Header dependencies ─────────────────────────
-include $(KERNEL_OBJS:.o=.d)
