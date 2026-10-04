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
#   make size      — Report segment use and fail if a segment is over budget
#   make test-host — Kernel code compiled for the host, with sanitizers
#   make coverage  — Line coverage of the host-tested kernel files
#   make test      — check + headless boot and shell integration tests in QEMU
#   make test-8086 — Boot and drive the kernel on an emulated 8086 (needs DOSBox-X)
#   make soak      — Churn threads, IPC and programs for SOAK_SECONDS (default 120)
#   make run       — Build and run in QEMU
#   make debug     — Run in QEMU with GDB server
#   make disasm    — Print the kernel's disassembly (pipe it into less)
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
NM      = $(CROSS)nm
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
                $(KERNEL_DIR)/serial.c \
                $(KERNEL_DIR)/disk.c \
                $(KERNEL_DIR)/floppy.c \
                $(KERNEL_DIR)/fat12.c \
                $(KERNEL_DIR)/exec.c \
                $(KERNEL_DIR)/string.c

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

# Example programs built with the SDK (sdk/examples/NAME.c → NAME.BIN)
PROGRAM_NAMES = hello primes clock threads where greet rogue
PROGRAM_DIR   = $(BUILD_DIR)/programs
PROGRAMS      = $(foreach name,$(PROGRAM_NAMES),$(PROGRAM_DIR)/$(shell echo $(name) | tr a-z A-Z).BIN)

# Files copied onto the floppy's FAT12 file system
DISK_FILES = disk/README.TXT $(PROGRAMS)

# ── Phony targets ──────────────────────────────
.PHONY: all toolchain boot kernel image check size test-host coverage test test-8086 soak disasm run debug dashboard clean

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

# The image is a FAT12 volume: boot sector, then Stage 2 and the kernel in
# the reserved sectors, then the files in DISK_FILES.
$(FLOPPY_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(DISK_FILES) tools/mkfat12.py | $(BUILD_DIR)
	@echo "=== Creating floppy image ==="
	$(PYTHON) tools/mkfat12.py $@ $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(DISK_FILES)
	@echo "  Stage 1: $$(wc -c < $(STAGE1_BIN)) bytes"
	@echo "  Stage 2: $$(wc -c < $(STAGE2_BIN)) bytes"
	@echo "  Kernel:  $$(wc -c < $(KERNEL_BIN)) bytes"

# ── Programs (SDK examples) ─────────────────────
SDK_CFLAGS = $(ARCHFLAGS) -ffreestanding -fno-builtin -Wall -Wextra -Os -Isdk/include -c

$(PROGRAM_DIR):
	mkdir -p $(PROGRAM_DIR)

$(PROGRAM_DIR)/crt0.o: sdk/lib/crt0.S | $(PROGRAM_DIR)
	$(CC) $(ARCHFLAGS) -c -o $@ $<

$(PROGRAM_DIR)/%.o: sdk/examples/%.c sdk/include/phoenix.h | $(PROGRAM_DIR)
	$(CC) $(SDK_CFLAGS) -o $@ $<

$(PROGRAM_DIR)/%.elf: $(PROGRAM_DIR)/%.o $(PROGRAM_DIR)/crt0.o sdk/program.ld
	$(LD) -T sdk/program.ld --no-check-sections -o $@ $(PROGRAM_DIR)/crt0.o $< \
	    $$($(CC) $(ARCHFLAGS) -print-libgcc-file-name)

define PROGRAM_RULE
$(PROGRAM_DIR)/$(shell echo $(1) | tr a-z A-Z).BIN: $(PROGRAM_DIR)/$(1).elf sdk/mkprog.py
	$(PYTHON) sdk/mkprog.py $$< $$@
endef
$(foreach name,$(PROGRAM_NAMES),$(eval $(call PROGRAM_RULE,$(name))))

.SECONDARY: $(foreach name,$(PROGRAM_NAMES),$(PROGRAM_DIR)/$(name).elf $(PROGRAM_DIR)/$(name).o)

# ── Verify 8086-only instructions ──────────────
check: $(KERNEL_ELF) $(PROGRAMS)
	$(PYTHON) tools/check8086.py $(OBJDUMP) $(KERNEL_ELF)
	@for name in $(PROGRAM_NAMES); do \
	    $(PYTHON) tools/check8086.py $(OBJDUMP) $(PROGRAM_DIR)/$$name.elf --start 0 || exit 1; \
	done

# ── Size report and budget (tools/size_report.py) ──
# SIZE_MARKDOWN=file also writes the report as Markdown (CI: the job summary)
size: $(KERNEL_BIN) $(PROGRAMS)
	$(PYTHON) tools/size_report.py $(NM) $(KERNEL_BIN) $(KERNEL_ELF) $(PROGRAMS) \
	    $(if $(SIZE_MARKDOWN),--markdown $(SIZE_MARKDOWN))

# ── Disassembly, for looking up an address from a panic ──
disasm: $(KERNEL_ELF)
	@$(OBJDUMP) -d -mi8086 $(KERNEL_ELF)

# ── Host-compiled kernel tests (tests/host/) ────
# Kernel files built unmodified for the host against a simulated
# machine (tests/host/machine.c), under AddressSanitizer and UBSan.
HOST_CC     ?= cc
HOST_DIR     = $(BUILD_DIR)/host
HOST_COMMON  = -std=gnu11 -g -Wall -Wextra -Wno-unused-parameter \
               -DPHOENIX_HOST -DCONFIG_TELEMETRY=0 -include tests/host/host.h -Iinclude -Ikernel
HOST_CFLAGS  = $(HOST_COMMON) -O1 -fno-omit-frame-pointer \
               -fsanitize=address,undefined -fno-sanitize-recover=all
HOST_HEADERS = $(wildcard tests/host/*.h include/*.h kernel/*.h)

# test name → its sources (memory.c is #included by its test)
HOST_SRC_sync   = tests/host/test_sync.c tests/host/machine.c kernel/sync.c kernel/ipc.c
HOST_SRC_memory = tests/host/test_memory.c tests/host/machine.c
HOST_SRC_fat12  = tests/host/test_fat12.c tests/host/machine.c kernel/fat12.c kernel/sync.c
HOST_TESTS      = sync memory fat12

# The heap aligns blocks to 4 bytes, enough on the 8086; on the host its
# header holds an 8-byte pointer. x86 does not mind, so neither does UBSan.
HOST_FLAGS_memory = -fno-sanitize=alignment

$(HOST_DIR) $(BUILD_DIR)/coverage:
	mkdir -p $@

define HOST_TEST_RULE
$(HOST_DIR)/test_$(1): $$(HOST_SRC_$(1)) kernel/memory.c $(HOST_HEADERS) | $(HOST_DIR)
	$$(HOST_CC) $$(HOST_CFLAGS) $$(HOST_FLAGS_$(1)) -o $$@ $$(HOST_SRC_$(1))
endef
$(foreach t,$(HOST_TESTS),$(eval $(call HOST_TEST_RULE,$(t))))

# test_fat12 reads the real floppy image and compares every file on it
test-host: $(foreach t,$(HOST_TESTS),$(HOST_DIR)/test_$(t)) $(FLOPPY_IMG)
	@echo "=== Host-compiled kernel tests ==="
	$(HOST_DIR)/test_sync
	$(HOST_DIR)/test_memory
	$(HOST_DIR)/test_fat12 $(FLOPPY_IMG) $(DISK_FILES)

# Line coverage of the same tests (gcov), without sanitizers
coverage: $(FLOPPY_IMG) | $(BUILD_DIR)/coverage
	@for t in $(HOST_TESTS); do \
	    case $$t in sync) src="$(HOST_SRC_sync)";; memory) src="$(HOST_SRC_memory)";; fat12) src="$(HOST_SRC_fat12)";; esac; \
	    $(HOST_CC) $(HOST_COMMON) -O0 --coverage -o $(BUILD_DIR)/coverage/test_$$t $$src || exit 1; \
	done
	rm -f $(BUILD_DIR)/coverage/*.gcda
	cd $(BUILD_DIR)/coverage && ./test_sync >/dev/null && ./test_memory >/dev/null && \
	    ./test_fat12 ../../$(FLOPPY_IMG) $(addprefix ../../,$(DISK_FILES)) >/dev/null
	$(PYTHON) tools/coverage_report.py $(BUILD_DIR)/coverage kernel/sync.c kernel/ipc.c kernel/memory.c kernel/fat12.c \
	    $(if $(COVERAGE_MARKDOWN),--markdown $(COVERAGE_MARKDOWN))

# ── Automated tests ─────────────────────────────
test: check size test-host $(FLOPPY_IMG)
	$(PYTHON) -m unittest discover -q -b -s bridge -t .
	@if command -v node >/dev/null; then node --test dashboard/test/*.test.js; \
	 else echo "  node not found: skipping dashboard model tests"; fi
	$(PYTHON) tools/test_image.py $(FLOPPY_IMG) $(KERNEL_BIN) $(DISK_FILES)
	$(PYTHON) tools/smoke_test.py $(FLOPPY_IMG)
	$(PYTHON) tools/integration_test.py $(FLOPPY_IMG)

# ── 8086 fidelity test ──────────────────────────
# QEMU emulates a 386+. This boots the image on DOSBox-X as an 8086.
test-8086: $(FLOPPY_IMG)
	$(PYTHON) tools/test_8086.py $(FLOPPY_IMG)

# ── Soak test ───────────────────────────────────
SOAK_SECONDS ?= 120

soak: $(FLOPPY_IMG)
	$(PYTHON) tools/soak_test.py $(FLOPPY_IMG) $(SOAK_SECONDS)

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
