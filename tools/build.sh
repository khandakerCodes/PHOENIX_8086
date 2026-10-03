#!/bin/bash
# ============================================================================
# Phoenix-8086 — Master Build Script
# ============================================================================
# Orchestrates the full build pipeline:
#   1. Clean previous build
#   2. Assemble bootloaders
#   3. Compile and link kernel
#   4. Create disk image
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "╔═══════════════════════════════════════════╗"
echo "║     Phoenix-8086 Build System             ║"
echo "╚═══════════════════════════════════════════╝"
echo ""

# Check for required tools
echo "[CHECK] Verifying toolchain..."
for tool in nasm make dd python3; do
    if ! command -v "$tool" &>/dev/null; then
        echo "  ERROR: '$tool' not found. Please install it."
        exit 1
    fi
    echo "  ✓ $tool"
done
echo ""

# Clean
echo "[CLEAN] Removing previous build..."
make clean 2>/dev/null || true
echo ""

# Build
echo "[BUILD] Building Phoenix-8086..."
make all
echo ""

echo "╔═══════════════════════════════════════════╗"
echo "║     Build Complete!                       ║"
echo "╚═══════════════════════════════════════════╝"
echo ""
echo "To run:  make run"
echo "To debug: make debug"
