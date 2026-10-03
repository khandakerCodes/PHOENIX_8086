#!/bin/bash
# ============================================================================
# Phoenix-8086 — Run Script
# ============================================================================
# Launches all three components:
#   1. QEMU with the Phoenix-8086 floppy image (serial to pipe)
#   2. Serial-to-WebSocket bridge (port 9090)
#   3. Dashboard HTTP server (port 8080)
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

# Build first if needed
if [ ! -f "build/phoenix8086.img" ]; then
    echo "[BUILD] Disk image not found, building..."
    make all
fi

echo "╔═══════════════════════════════════════════╗"
echo "║     Phoenix-8086 Launch                   ║"
echo "╚═══════════════════════════════════════════╝"
echo ""

# Create a named pipe for serial communication
SERIAL_PIPE="/tmp/phoenix_serial"
rm -f "$SERIAL_PIPE.in" "$SERIAL_PIPE.out"
mkfifo "$SERIAL_PIPE.in" 2>/dev/null || true
mkfifo "$SERIAL_PIPE.out" 2>/dev/null || true

# Start dashboard HTTP server
echo "[START] Dashboard at http://localhost:8080"
cd dashboard && python3 -m http.server 8080 &
DASHBOARD_PID=$!
cd "$PROJECT_DIR"

# Start the serial-to-WebSocket bridge
echo "[START] Telemetry bridge on ws://localhost:9090"
python3 bridge/serial_ws_bridge.py &
BRIDGE_PID=$!

# Start QEMU
echo "[START] QEMU with Phoenix-8086"
echo ""
qemu-system-i386 \
    -drive file=build/phoenix8086.img,format=raw,if=floppy \
    -boot a \
    -serial stdio \
    -m 1M \
    -display gtk

# Cleanup on exit
echo ""
echo "[STOP] Shutting down..."
kill $DASHBOARD_PID 2>/dev/null || true
kill $BRIDGE_PID 2>/dev/null || true
rm -f "$SERIAL_PIPE.in" "$SERIAL_PIPE.out"
echo "Done."
