#!/bin/bash
# SPDX-License-Identifier: MIT
# ============================================================================
# Phoenix-8086 — Master Launch Script
# ============================================================================
#
# Single script that builds and launches all project components:
#   1. Builds the kernel floppy image (incremental)
#   2. Starts the Visual Dashboard (HTTP server on port 8080)
#   3. Starts the Telemetry Bridge (WebSocket on port 9090)
#   4. Launches QEMU with the Phoenix-8086 floppy image
#
# Usage:
#   ./phoenix.sh          — Build and launch everything
#   ./phoenix.sh build    — Build only
#   ./phoenix.sh clean    — Clean build artifacts
#
# Press Ctrl+C to shut down all components gracefully.
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ── Colors ──────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# ── PIDs to track for cleanup ───────────────────
DASHBOARD_PID=""
BRIDGE_PID=""
QEMU_PID=""

# ── Cleanup on exit ─────────────────────────────
cleanup() {
    echo ""
    echo -e "${YELLOW}[SHUTDOWN]${RESET} Stopping all Phoenix-8086 services..."

    if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" 2>/dev/null; then
        echo -e "  ${RED}●${RESET} Stopping QEMU (PID $QEMU_PID)"
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
    fi

    if [ -n "$BRIDGE_PID" ] && kill -0 "$BRIDGE_PID" 2>/dev/null; then
        echo -e "  ${RED}●${RESET} Stopping Telemetry Bridge (PID $BRIDGE_PID)"
        kill "$BRIDGE_PID" 2>/dev/null || true
        wait "$BRIDGE_PID" 2>/dev/null || true
    fi

    if [ -n "$DASHBOARD_PID" ] && kill -0 "$DASHBOARD_PID" 2>/dev/null; then
        echo -e "  ${RED}●${RESET} Stopping Dashboard Server (PID $DASHBOARD_PID)"
        kill "$DASHBOARD_PID" 2>/dev/null || true
        wait "$DASHBOARD_PID" 2>/dev/null || true
    fi

    echo -e "${GREEN}[DONE]${RESET} All services stopped. Goodbye."
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT

# ── Banner ──────────────────────────────────────
print_banner() {
    echo -e "${RED}"
    echo "  ____  _                      _         ___   ___   ___   __"
    echo " |  _ \| |__   ___   ___ _ __ (_)_  __  ( _ ) / _ \ ( _ ) / /_"
    echo " | |_) | '_ \ / _ \ / _ \ '_ \| \ \/ /  / _ \| | | |/ _ \| '_ \\"
    echo " |  __/| | | | (_) |  __/ | | | |>  <  | (_) | |_| | (_) | (_) |"
    echo " |_|   |_| |_|\___/ \___|_| |_|_/_/\_\  \___/ \___/ \___/ \___/"
    echo -e "${RESET}"
    echo -e "${BOLD}  Bare-Metal Preemptive Microkernel for the Intel 8086${RESET}"
    echo ""
}

# ── Check dependencies ──────────────────────────
check_deps() {
    local missing=0

    for tool in nasm make dd qemu-system-i386 python3; do
        if ! command -v "$tool" &>/dev/null; then
            echo -e "  ${RED}✗${RESET} $tool — ${RED}NOT FOUND${RESET}"
            missing=1
        else
            echo -e "  ${GREEN}✓${RESET} $tool"
        fi
    done

    if [ "$missing" -eq 1 ]; then
        echo ""
        echo -e "${RED}[ERROR]${RESET} Missing dependencies. Install with:"
        echo "  sudo apt update && sudo apt install -y nasm make qemu-system-x86 python3"
        echo "  make toolchain   # fetches ia16-elf-gcc into .toolchain/"
        exit 1
    fi
}

# ── Check ports ─────────────────────────────────
# Only processes started by this script are ever stopped. If a port
# is already taken, say so and let the user decide what to do.
check_ports() {
    local busy=0

    for port in 8080 9090 9876; do
        if (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            echo -e "  ${RED}✗${RESET} Port $port is already in use"
            busy=1
        fi
    done

    if [ "$busy" -eq 1 ]; then
        echo ""
        echo -e "${RED}[ERROR]${RESET} Free the ports above (dashboard 8080, bridge 9090, serial 9876) and try again."
        exit 1
    fi
}

# ── Build ───────────────────────────────────────
build_project() {
    echo -e "${CYAN}[BUILD]${RESET} Compiling Phoenix-8086..."

    local output
    if output=$(make image 2>&1); then
        echo "$output" | tail -5
        echo -e "${GREEN}[BUILD]${RESET} Build successful!"
    else
        echo "$output" | tail -20
        echo -e "${RED}[BUILD]${RESET} Build failed. Check errors above."
        exit 1
    fi
    echo ""
}

# ── Start Dashboard ─────────────────────────────
start_dashboard() {
    python3 -m http.server 8080 --directory dashboard &>/dev/null &
    DASHBOARD_PID=$!
    sleep 0.5

    if kill -0 "$DASHBOARD_PID" 2>/dev/null; then
        echo -e "  ${GREEN}●${RESET} Dashboard        → ${BOLD}http://localhost:8080${RESET}  (PID $DASHBOARD_PID)"
    else
        echo -e "  ${RED}●${RESET} Dashboard        → ${RED}FAILED TO START${RESET}"
        DASHBOARD_PID=""
    fi
}

# ── Start Telemetry Bridge ──────────────────────
start_bridge() {
    python3 bridge/serial_ws_bridge.py &>/dev/null &
    BRIDGE_PID=$!
    sleep 0.5

    if kill -0 "$BRIDGE_PID" 2>/dev/null; then
        echo -e "  ${GREEN}●${RESET} Telemetry Bridge → ${BOLD}ws://localhost:9090${RESET}   (PID $BRIDGE_PID)"
    else
        echo -e "  ${RED}●${RESET} Telemetry Bridge → ${RED}FAILED TO START${RESET}"
        BRIDGE_PID=""
    fi
}

# ── Start QEMU ──────────────────────────────────
start_qemu() {
    echo ""
    echo -e "${CYAN}[LAUNCH]${RESET} Starting QEMU emulator..."
    echo -e "  ${BLUE}→${RESET} Floppy image: build/phoenix8086.img"
    echo -e "  ${BLUE}→${RESET} Telemetry & Serial mirrored to Web Dashboard (ws://localhost:9090)"
    echo -e "  ${BLUE}→${RESET} Press ${BOLD}Ctrl+C${RESET} to shut everything down"
    echo ""

    qemu-system-i386 \
        -drive file=build/phoenix8086.img,format=raw,if=floppy \
        -boot a \
        -serial tcp:127.0.0.1:9876,server,nowait \
        -m 1M &
    QEMU_PID=$!

    # Wait for QEMU to exit
    wait "$QEMU_PID" 2>/dev/null || true
    QEMU_PID=""
}

# ════════════════════════════════════════════════
# Main
# ════════════════════════════════════════════════

print_banner

# Handle subcommands
case "${1:-}" in
    clean)
        echo -e "${CYAN}[CLEAN]${RESET} Removing build artifacts..."
        make clean
        exit 0
        ;;
    build)
        echo -e "${CYAN}[CHECK]${RESET} Verifying toolchain..."
        check_deps
        echo ""
        build_project
        exit 0
        ;;
    *)
        ;;
esac

# ── Step 1: Check tools ────────────────────────
echo -e "${CYAN}[CHECK]${RESET} Verifying toolchain..."
check_deps
echo ""

# ── Step 2: Build (incremental) ────────────────
build_project

# ── Step 3: Make sure our ports are free ───────
check_ports

# ── Step 4: Launch services ────────────────────
echo -e "${CYAN}[SERVICES]${RESET} Starting background services..."
start_dashboard
start_bridge
echo ""

# ── Step 5: Launch QEMU (foreground) ───────────
start_qemu
