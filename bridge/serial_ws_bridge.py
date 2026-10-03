#!/usr/bin/env python3
"""
Phoenix-8086 — Serial-to-WebSocket Telemetry Bridge

Reads telemetry packets from QEMU's serial output (stdin or pipe),
parses the binary framing, and relays structured JSON data over
WebSocket to the visual dashboard.

Packet format:
    [START_BYTE (0xFE)] [TYPE] [LENGTH] [DATA...] [CHECKSUM]

WebSocket server runs on port 9090.
"""

import asyncio
import json
import sys
import struct

# Try to import websockets; fall back gracefully if not available
try:
    import websockets
    HAS_WEBSOCKETS = True
except ImportError:
    HAS_WEBSOCKETS = False
    print("[BRIDGE] Warning: 'websockets' module not found.")
    print("[BRIDGE] Install with: pip3 install websockets")
    print("[BRIDGE] Running in log-only mode (no WebSocket relay).")

# ── Constants ──────────────────────────────────
START_BYTE = 0xFE
WS_PORT = 9090

# Telemetry type names
TYPE_NAMES = {
    0x01: "BOOT_STAGE",
    0x02: "THREAD_EVENT",
    0x03: "CONTEXT_SWITCH",
    0x04: "IRQ_COUNTER",
    0x05: "REG_SNAPSHOT",
    0x06: "MEM_SUMMARY",
    0x07: "FAULT",
}

# ── Connected WebSocket clients ────────────────
connected_clients = set()


def parse_packet(raw_bytes):
    """Parse a telemetry packet into a JSON-serializable dict."""
    if len(raw_bytes) < 4:
        return None

    start = raw_bytes[0]
    if start != START_BYTE:
        return None

    pkt_type = raw_bytes[1]
    length = raw_bytes[2]

    if len(raw_bytes) < 3 + length + 1:
        return None

    data = raw_bytes[3:3 + length]
    checksum = raw_bytes[3 + length]

    # Verify checksum
    calc_checksum = pkt_type ^ length
    for b in data:
        calc_checksum ^= b

    if calc_checksum != checksum:
        return None

    # Build JSON payload
    type_name = TYPE_NAMES.get(pkt_type, f"UNKNOWN_{pkt_type:02X}")
    payload = {
        "type": type_name,
        "data": list(data),
        "raw_type": pkt_type,
    }

    # Parse specific types
    if pkt_type == 0x01:  # BOOT_STAGE
        payload["stage"] = data[0] if data else 0

    elif pkt_type == 0x02:  # THREAD_EVENT
        if len(data) >= 2:
            payload["event"] = data[0]
            payload["tid"] = data[1]

    elif pkt_type == 0x03:  # CONTEXT_SWITCH
        if len(data) >= 2:
            payload["from_tid"] = data[0]
            payload["to_tid"] = data[1]

    elif pkt_type == 0x04:  # IRQ_COUNTER
        if len(data) >= 8:
            payload["timer"] = struct.unpack_from("<H", bytes(data), 0)[0]
            payload["keyboard"] = struct.unpack_from("<H", bytes(data), 2)[0]
            payload["syscall"] = struct.unpack_from("<H", bytes(data), 4)[0]
            payload["context_switches"] = struct.unpack_from("<H", bytes(data), 6)[0]

    elif pkt_type == 0x07:  # FAULT
        if len(data) >= 5:
            payload["tid"] = data[0]
            payload["ip"] = struct.unpack_from("<H", bytes(data), 1)[0]
            payload["cs"] = struct.unpack_from("<H", bytes(data), 3)[0]

    return payload


async def broadcast(message):
    """Send a message to all connected WebSocket clients."""
    if connected_clients:
        msg_json = json.dumps(message)
        await asyncio.gather(
            *[client.send(msg_json) for client in connected_clients],
            return_exceptions=True,
        )


async def ws_handler(websocket):
    """Handle a new WebSocket connection."""
    connected_clients.add(websocket)
    print(f"[BRIDGE] Client connected ({len(connected_clients)} total)")
    try:
        async for message in websocket:
            # Dashboard might send commands back; log them
            print(f"[BRIDGE] Received from client: {message}")
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.discard(websocket)
        print(f"[BRIDGE] Client disconnected ({len(connected_clients)} total)")


async def get_input_stream():
    """Attempt to connect to QEMU serial TCP port 9876; fallback to stdin if unavailable."""
    try:
        reader, writer = await asyncio.wait_for(asyncio.open_connection('127.0.0.1', 9876), timeout=1.0)
        print("[BRIDGE] Connected to QEMU serial TCP port 9876")
        return reader
    except (asyncio.TimeoutError, ConnectionRefusedError, OSError):
        loop = asyncio.get_event_loop()
        reader = asyncio.StreamReader()
        protocol = asyncio.StreamReaderProtocol(reader)
        await loop.connect_read_pipe(lambda: protocol, sys.stdin.buffer)
        return reader


async def serial_reader():
    """Read serial data from TCP or stdin and parse telemetry packets & text console logs."""
    reader = await get_input_stream()
    buffer = bytearray()
    line_buf = bytearray()

    while True:
        try:
            chunk = await reader.read(1024)
            if not chunk:
                await asyncio.sleep(0.5)
                reader = await get_input_stream()
                continue
        except Exception:
            await asyncio.sleep(0.5)
            reader = await get_input_stream()
            continue

        buffer.extend(chunk)

        while len(buffer) > 0:
            if buffer[0] == START_BYTE:
                if len(buffer) < 4:
                    break
                length = buffer[2]
                packet_size = 3 + length + 1
                if len(buffer) < packet_size:
                    break
                packet = parse_packet(buffer[:packet_size])
                buffer = buffer[packet_size:]
                if packet:
                    print(f"[TELEMETRY] {packet['type']}: {json.dumps(packet)}")
                    await broadcast(packet)
            else:
                b = buffer.pop(0)
                if b == ord('\n') or b == ord('\r'):
                    if line_buf:
                        text_line = line_buf.decode('utf-8', errors='replace').rstrip()
                        if text_line:
                            print(f"[CONSOLE] {text_line}")
                            await broadcast({"type": "CONSOLE", "text": text_line})
                        line_buf.clear()
                else:
                    line_buf.append(b)


async def main():
    """Main entry point: start WebSocket server and serial reader."""
    if HAS_WEBSOCKETS:
        server = await websockets.serve(ws_handler, "localhost", WS_PORT)
        print(f"[BRIDGE] WebSocket server on ws://localhost:{WS_PORT}")
    else:
        server = None

    print("[BRIDGE] Reading telemetry from stdin...")

    await serial_reader()

    if server:
        server.close()


if __name__ == "__main__":
    print("╔═══════════════════════════════════════════╗")
    print("║  Phoenix-8086 Telemetry Bridge            ║")
    print("╚═══════════════════════════════════════════╝")
    asyncio.run(main())
