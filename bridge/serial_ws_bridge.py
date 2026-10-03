#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Phoenix-8086 — Serial-to-WebSocket Telemetry Bridge

Live mode (default): connects to the kernel's serial port (QEMU started
with "-serial tcp:127.0.0.1:9876,server,nowait"), decodes telemetry
protocol v1, and relays each message as JSON to every dashboard
connected over WebSocket. Text typed in a dashboard is written back to
the serial port. With --capture the session is also recorded to a file.

Replay mode (--replay FILE): no kernel involved; a recorded capture is
played back to the dashboards with its original timing (see --speed).

The bridge only relays what the kernel sent. It adds one message type
of its own, BRIDGE, which describes the link:

    {"type": "BRIDGE", "mode": "live" | "replay", "serial": bool,
     "frames": n, "bad_frames": n, "lost_frames": n, "reset": bool}

"reset" is true when a new session starts (the dashboard clears itself).
New dashboards first receive the messages of the session so far.
"""

import argparse
import asyncio
import collections
import json
import sys
import time

try:
    from bridge.capture import CaptureWriter, read_capture
    from bridge.protocol import Decoder
except ImportError:     # run as a script: python3 bridge/serial_ws_bridge.py
    from capture import CaptureWriter, read_capture
    from protocol import Decoder

BACKLOG_SIZE = 20000        # Messages kept for dashboards that connect late
MAX_INPUT_LENGTH = 256      # Characters accepted from a dashboard at once
RETRY_SECONDS = 1.0
STATUS_SECONDS = 1.0


def sanitize_input(text):
    """
    Keystrokes a dashboard may send to the kernel: printable ASCII,
    Enter and Backspace. Anything else is dropped.
    """
    allowed = bytearray()
    for ch in str(text)[:MAX_INPUT_LENGTH]:
        code = ord(ch)
        if ch in ("\n", "\r"):
            allowed.append(0x0D)
        elif ch in ("\b", "\x7f"):
            allowed.append(0x08)
        elif 0x20 <= code < 0x7F:
            allowed.append(code)
    return bytes(allowed)


class Bridge:
    def __init__(self, mode):
        self.mode = mode
        self.clients = set()
        self.backlog = collections.deque(maxlen=BACKLOG_SIZE)
        self.decoder = Decoder()
        self.serial_writer = None
        self.capture = None
        self.started = time.monotonic()

    # ── Messages out ────────────────────────────

    def status(self, reset=False):
        return {"type": "BRIDGE", "mode": self.mode,
                "serial": self.serial_writer is not None,
                "frames": self.decoder.frames,
                "bad_frames": self.decoder.bad_frames,
                "lost_frames": self.decoder.lost_frames,
                "reset": reset}

    async def send_all(self, message):
        if not self.clients:
            return
        text = json.dumps(message)
        await asyncio.gather(*(client.send(text) for client in self.clients),
                             return_exceptions=True)

    async def publish(self, message):
        """Relay one kernel message: backlog, capture file, dashboards."""
        self.backlog.append(message)
        if self.capture:
            self.capture.write(time.monotonic() - self.started, message)
        await self.send_all(message)

    async def new_session(self):
        self.backlog.clear()
        self.decoder = Decoder()
        await self.send_all(self.status(reset=True))

    # ── Dashboards ──────────────────────────────

    async def handle_client(self, websocket):
        await websocket.send(json.dumps(self.status(reset=True)))
        for message in list(self.backlog):
            await websocket.send(json.dumps(message))
        self.clients.add(websocket)
        print(f"[BRIDGE] Dashboard connected ({len(self.clients)} total)")
        try:
            async for raw in websocket:
                await self.handle_client_message(raw)
        except Exception:
            pass
        finally:
            self.clients.discard(websocket)
            print(f"[BRIDGE] Dashboard disconnected ({len(self.clients)} total)")

    async def handle_client_message(self, raw):
        try:
            message = json.loads(raw)
        except (ValueError, TypeError):
            return
        if not isinstance(message, dict) or message.get("type") != "input":
            return
        data = sanitize_input(message.get("text", ""))
        if data and self.serial_writer is not None:
            self.serial_writer.write(data)
            await self.serial_writer.drain()

    # ── Live mode ───────────────────────────────

    async def run_live(self, host, port):
        while True:
            try:
                reader, writer = await asyncio.open_connection(host, port)
            except OSError:
                await asyncio.sleep(RETRY_SECONDS)
                continue

            print(f"[BRIDGE] Connected to serial port at {host}:{port}")
            self.serial_writer = writer
            await self.new_session()
            try:
                while True:
                    chunk = await reader.read(4096)
                    if not chunk:
                        break
                    for message in self.decoder.feed(chunk):
                        await self.publish(message)
            except OSError:
                pass
            finally:
                self.serial_writer = None
                writer.close()
                print("[BRIDGE] Serial connection closed; waiting for the kernel")
                await self.send_all(self.status())

    async def run_status(self):
        while True:
            await asyncio.sleep(STATUS_SECONDS)
            await self.send_all(self.status())

    # ── Replay mode ─────────────────────────────

    async def run_replay(self, path, speed, loop):
        entries = read_capture(path)
        print(f"[BRIDGE] Replaying {len(entries)} messages from {path} at {speed}x")
        while True:
            await self.new_session()
            start = time.monotonic()
            for seconds, message in entries:
                delay = seconds / speed - (time.monotonic() - start)
                if delay > 0:
                    await asyncio.sleep(delay)
                self.decoder.frames += 1
                await self.publish(message)
            if not loop:
                print("[BRIDGE] Replay finished; dashboards keep the final state")
                await asyncio.Event().wait()
            await asyncio.sleep(2.0)


async def serve(args):
    import websockets

    bridge = Bridge("replay" if args.replay else "live")
    if args.capture and not args.replay:
        bridge.capture = CaptureWriter(args.capture)
        print(f"[BRIDGE] Recording to {args.capture}")

    async with websockets.serve(bridge.handle_client, args.ws_host, args.ws_port):
        print(f"[BRIDGE] WebSocket server on ws://{args.ws_host}:{args.ws_port}")
        if args.replay:
            await bridge.run_replay(args.replay, args.speed, args.loop)
        else:
            await asyncio.gather(bridge.run_live(args.serial_host, args.serial_port),
                                 bridge.run_status())


def main():
    parser = argparse.ArgumentParser(description="Phoenix-8086 telemetry bridge")
    parser.add_argument("--serial-host", default="127.0.0.1")
    parser.add_argument("--serial-port", type=int, default=9876)
    parser.add_argument("--ws-host", default="localhost")
    parser.add_argument("--ws-port", type=int, default=9090)
    parser.add_argument("--capture", metavar="FILE", help="record the live session to FILE")
    parser.add_argument("--replay", metavar="FILE", help="replay a capture instead of going live")
    parser.add_argument("--speed", type=float, default=1.0, help="replay speed factor (default 1)")
    parser.add_argument("--loop", action="store_true", help="restart the replay when it ends")
    args = parser.parse_args()

    if args.speed <= 0:
        parser.error("--speed must be positive")

    try:
        import websockets  # noqa: F401
    except ImportError:
        sys.exit("The 'websockets' package is required: pip install -r bridge/requirements.txt")

    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
