// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — In-browser kernel
 *
 * Boots the real floppy image in v86, a PC emulator that runs in the
 * page, and decodes the kernel's serial output with the same protocol
 * the bridge uses. The dashboard then shows a kernel running on the
 * visitor's own machine, with nothing to install.
 *
 * v86 emulates a 386-class PC, like QEMU: this shows the kernel
 * working, not that it is 8086-clean (make test-8086 does that).
 *
 * The emulator files live in emulator/ and are not part of the
 * repository; tools/build_site.sh assembles them.
 */
(function (root) {
    'use strict';

    const FILES = {
        script: 'emulator/libv86.js',
        wasm: 'emulator/v86.wasm',
        bios: 'emulator/seabios.bin',
        vgaBios: 'emulator/vgabios.bin',
        image: 'emulator/phoenix8086.img',
    };
    const STATUS_INTERVAL = 1000;
    const MAX_INPUT_LENGTH = 256;

    /* Keystrokes the page may send: printable ASCII, Enter and Backspace */
    function sanitize(text) {
        let result = '';
        const input = String(text).slice(0, MAX_INPUT_LENGTH);
        for (let i = 0; i < input.length; i++) {
            const code = input.charCodeAt(i);
            if (input[i] === '\n' || input[i] === '\r') {
                result += '\r';
            } else if (input[i] === '\b' || code === 0x7F) {
                result += '\b';
            } else if (code >= 0x20 && code < 0x7F) {
                result += input[i];
            }
        }
        return result;
    }

    function loadScript(doc, url) {
        return new Promise(function (resolve, reject) {
            const script = doc.createElement('script');
            script.src = url;
            script.onload = resolve;
            script.onerror = function () { reject(new Error('could not load ' + url)); };
            doc.head.appendChild(script);
        });
    }

    /**
     * Start the emulator. `deliver` receives the same messages a bridge
     * would send. Returns a promise for { send(text), stop() }.
     */
    function start(deliver, doc) {
        const ready = root.V86 ? Promise.resolve() : loadScript(doc, FILES.script);

        return ready.then(function () {
            const decoder = new root.PhoenixProtocol.Decoder();
            const emulator = new root.V86({
                wasm_path: FILES.wasm,
                bios: { url: FILES.bios },
                vga_bios: { url: FILES.vgaBios },
                fda: { url: FILES.image },
                boot_order: 0x321,
                memory_size: 2 * 1024 * 1024,
                vga_memory_size: 2 * 1024 * 1024,
                autostart: true,
            });

            function status(reset) {
                return { type: 'BRIDGE', mode: 'emulator', serial: true, reset: reset,
                         frames: decoder.frames, bad_frames: decoder.badFrames,
                         lost_frames: decoder.lostFrames };
            }

            deliver(status(true));
            emulator.add_listener('serial0-output-byte', function (byte) {
                decoder.feed([byte]).forEach(deliver);
            });
            const timer = setInterval(function () { deliver(status(false)); }, STATUS_INTERVAL);

            return {
                send: function (text) {
                    const keys = sanitize(text);
                    if (keys) emulator.serial0_send(keys);
                },
                stop: function () {
                    clearInterval(timer);
                    if (emulator.destroy) emulator.destroy();
                },
            };
        });
    }

    root.PhoenixBrowser = { start: start, sanitize: sanitize, FILES: FILES };
}(typeof self !== 'undefined' ? self : this));
