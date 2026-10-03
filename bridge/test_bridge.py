# SPDX-License-Identifier: MIT
"""Tests for the bridge: input filtering, capture files, and the relay end to end."""

import asyncio
import json
import os
import tempfile
import unittest

from bridge.capture import CaptureWriter, read_capture
from bridge.protocol import TYPE_IDS, encode_frame
from bridge.serial_ws_bridge import Bridge, sanitize_input

try:
    import websockets
except ImportError:
    websockets = None


class SanitizeInputTest(unittest.TestCase):
    def test_printable_text_and_enter(self):
        self.assertEqual(sanitize_input("ps\n"), b"ps\r")

    def test_control_characters_are_dropped(self):
        self.assertEqual(sanitize_input("a\x00\x1b[2J\x03b"), b"a[2Jb")

    def test_backspace(self):
        self.assertEqual(sanitize_input("x\b\x7f"), b"x\x08\x08")

    def test_non_ascii_is_dropped(self):
        self.assertEqual(sanitize_input("é~☃"), b"~")

    def test_length_is_limited(self):
        self.assertEqual(len(sanitize_input("a" * 5000)), 256)

    def test_non_string_input(self):
        self.assertEqual(sanitize_input(None), b"None")
        self.assertEqual(sanitize_input(12), b"12")


class CaptureTest(unittest.TestCase):
    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "session.jsonl")
            writer = CaptureWriter(path)
            writer.write(0.5, {"type": "BOOT_STAGE", "stage": 1})
            writer.write(1.25, {"type": "CONSOLE", "text": "hi\n"})
            writer.close()
            self.assertEqual(read_capture(path),
                             [(0.5, {"type": "BOOT_STAGE", "stage": 1}),
                              (1.25, {"type": "CONSOLE", "text": "hi\n"})])

    def test_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "other.jsonl")
            with open(path, "w") as f:
                f.write('{"something": "else"}\n')
            with self.assertRaises(ValueError):
                read_capture(path)


@unittest.skipIf(websockets is None, "websockets package not installed")
class RelayTest(unittest.IsolatedAsyncioTestCase):
    """A fake kernel on a TCP socket, the bridge, and a WebSocket client."""

    async def asyncSetUp(self):
        self.kernel_writer = None
        self.kernel_received = asyncio.Queue()
        self.kernel_connected = asyncio.Event()

        async def kernel(reader, writer):
            self.kernel_writer = writer
            self.kernel_connected.set()
            while True:
                data = await reader.read(100)
                if not data:
                    break
                await self.kernel_received.put(data)

        self.serial = await asyncio.start_server(kernel, "127.0.0.1", 0)
        serial_port = self.serial.sockets[0].getsockname()[1]

        self.tmp = tempfile.TemporaryDirectory()
        self.capture_path = os.path.join(self.tmp.name, "capture.jsonl")
        self.bridge = Bridge("live")
        self.bridge.capture = CaptureWriter(self.capture_path)
        self.ws_server = await websockets.serve(self.bridge.handle_client, "127.0.0.1", 0)
        self.ws_port = self.ws_server.sockets[0].getsockname()[1]
        self.live = asyncio.create_task(self.bridge.run_live("127.0.0.1", serial_port))
        await asyncio.wait_for(self.kernel_connected.wait(), 5)

    async def asyncTearDown(self):
        self.live.cancel()
        self.ws_server.close()
        self.serial.close()
        self.bridge.capture.close()
        self.tmp.cleanup()

    async def receive(self, ws, type_name):
        while True:
            message = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if message["type"] == type_name:
                return message

    async def test_relay_capture_backlog_and_input(self):
        async with websockets.connect(f"ws://127.0.0.1:{self.ws_port}") as ws:
            status = await self.receive(ws, "BRIDGE")
            self.assertEqual((status["mode"], status["reset"]), ("live", True))

            # Kernel → dashboard, including a damaged frame that must not get through
            damaged = bytearray(encode_frame(TYPE_IDS["BOOT_STAGE"], 0, 1, b"\x09"))
            damaged[4] ^= 0xFF
            self.kernel_writer.write(bytes(damaged))
            self.kernel_writer.write(encode_frame(TYPE_IDS["BOOT_STAGE"], 1, 2, b"\x03"))
            self.kernel_writer.write(encode_frame(TYPE_IDS["CONSOLE"], 2, 3, b"hello"))
            await self.kernel_writer.drain()

            self.assertEqual((await self.receive(ws, "BOOT_STAGE"))["stage"], 3)
            self.assertEqual((await self.receive(ws, "CONSOLE"))["text"], "hello")
            self.assertEqual(self.bridge.decoder.bad_frames, 1)

            # Dashboard → kernel, filtered
            await ws.send(json.dumps({"type": "input", "text": "ps\x1b\n"}))
            await ws.send(json.dumps({"type": "other", "text": "ignored"}))
            await ws.send("not json")
            self.assertEqual(await asyncio.wait_for(self.kernel_received.get(), 5), b"ps\r")

        # A dashboard that connects later gets the session so far
        async with websockets.connect(f"ws://127.0.0.1:{self.ws_port}") as late:
            await self.receive(late, "BRIDGE")
            self.assertEqual((await self.receive(late, "BOOT_STAGE"))["stage"], 3)
            self.assertEqual((await self.receive(late, "CONSOLE"))["text"], "hello")

        # The capture holds exactly what was relayed
        types = [message["type"] for _, message in read_capture(self.capture_path)]
        self.assertEqual(types, ["BOOT_STAGE", "CONSOLE"])


@unittest.skipIf(websockets is None, "websockets package not installed")
class ReplayTest(unittest.IsolatedAsyncioTestCase):
    async def test_replay_delivers_capture_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "capture.jsonl")
            writer = CaptureWriter(path)
            for i in range(5):
                writer.write(i * 0.5, {"type": "BOOT_STAGE", "stage": i})
            writer.close()

            bridge = Bridge("replay")
            server = await websockets.serve(bridge.handle_client, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            async with websockets.connect(f"ws://127.0.0.1:{port}") as ws:
                first = json.loads(await asyncio.wait_for(ws.recv(), 5))
                self.assertEqual(first["mode"], "replay")
                task = asyncio.create_task(bridge.run_replay(path, speed=50.0, loop=False))
                stages = []
                while len(stages) < 5:
                    message = json.loads(await asyncio.wait_for(ws.recv(), 5))
                    if message["type"] == "BOOT_STAGE":
                        stages.append(message["stage"])
                self.assertEqual(stages, [0, 1, 2, 3, 4])
                task.cancel()
            server.close()


if __name__ == "__main__":
    unittest.main()
